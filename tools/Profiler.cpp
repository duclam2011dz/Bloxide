#include <bloxide/World.hpp>
#include <bloxide/SurfaceHierarchy.hpp>
#include <bloxide/Telemetry.hpp>
#include <bloxide/Trace.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
#include <iomanip>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace bloxide;
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double percentile(std::vector<double> values,unsigned p){std::sort(values.begin(),values.end());return values.empty()?0:values[(values.size()-1)*p/100];}
std::string read(const std::filesystem::path&path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
#ifdef _WIN32
std::wstring quote(const std::wstring& text){
    std::wstring out=L"\"";unsigned slashes=0;
    for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}
        out.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;out.push_back(c);}
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
int launch(const std::filesystem::path& exe,const std::vector<std::wstring>& args,const std::filesystem::path& log,unsigned timeout){
    std::wstring command=quote(exe.wstring());for(auto&arg:args)command+=L" "+quote(arg);
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE file=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("child log open failed");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=file;startup.hStdError=file;startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);CloseHandle(file);
    if(!ok)throw std::runtime_error("benchmark child launch failed");
    auto status=WaitForSingleObject(process.hProcess,timeout);DWORD exit=1;
    if(status==WAIT_TIMEOUT){TerminateProcess(process.hProcess,2);WaitForSingleObject(process.hProcess,5000);}
    GetExitCodeProcess(process.hProcess,&exit);CloseHandle(process.hThread);CloseHandle(process.hProcess);return int(exit);
}
#else
int launch(const std::filesystem::path&,const std::vector<std::wstring>&,const std::filesystem::path&,unsigned){
    throw std::runtime_error("render suite launcher currently targets Windows");
}
#endif
}
int main(int argc,char**argv){try{
    std::string suite="all",backendText="scalar";NoiseBackend backend=NoiseBackend::Scalar;bool quick=false,lod=true;
    unsigned repeat=5;double warmup=5,duration=30;std::uint64_t seed=130;std::filesystem::path out="build/benchmark/v1.4";
    for(int i=1;i<argc;++i){std::string a=argv[i];
        if(a=="--help"){std::cout<<"BloxideProfiler --suite noise|worldgen|streaming|fluid|render|all [--quick] [--seed UINT64] [--repeat 1..20] [--warmup SECONDS] [--duration SECONDS] [--backend scalar|sse2|avx2|auto] [--lod on|off] [--output DIRECTORY]\nDefault: five repeats, 5 s warmup, 30 s runtime sampling.\n";return 0;}
        else if(a=="--quick"){quick=true;repeat=1;warmup=0.5;duration=2;}
        else if(a=="--suite"&&i+1<argc)suite=argv[++i];
        else if(a=="--output"&&i+1<argc)out=argv[++i];
        else if(a=="--seed"&&i+1<argc)seed=WorldSettings::parseSeed(argv[++i]);
        else if(a=="--repeat"&&i+1<argc){auto n=WorldSettings::parseSeed(argv[++i]);if(n<1||n>20)throw std::invalid_argument("repeat 1..20");repeat=unsigned(n);}
        else if((a=="--warmup"||a=="--duration")&&i+1<argc){std::string value=argv[++i];std::size_t used=0;double n=std::stod(value,&used);
            if(used!=value.size())throw std::invalid_argument("invalid benchmark time");if(a=="--warmup")warmup=n;else duration=n;}
        else if(a=="--lod"&&i+1<argc){std::string v=argv[++i];if(v!="on"&&v!="off")throw std::invalid_argument("LOD on|off");lod=v=="on";}
        else if(a=="--backend"&&i+1<argc){backendText=argv[++i];backend=backendText=="scalar"?NoiseBackend::Scalar:backendText=="sse2"?NoiseBackend::SSE2:backendText=="avx2"?NoiseBackend::AVX2:backendText=="auto"?NoiseBackend::Auto:throw std::invalid_argument("backend");}
        else throw std::invalid_argument("BloxideProfiler --suite noise|worldgen|streaming|fluid|render|all [--quick] [--seed] [--repeat] [--warmup] [--duration] [--backend] [--lod] [--output]");
    }
    if(suite!="all"&&suite!="noise"&&suite!="worldgen"&&suite!="streaming"&&suite!="fluid"&&suite!="render")throw std::invalid_argument("invalid suite");
    if(repeat<1||repeat>20||!std::isfinite(warmup)||!std::isfinite(duration)||warmup<0||warmup>600||duration<=0||duration>600||!noiseBackendSupported(backend))throw std::invalid_argument("benchmark configuration");
    std::filesystem::create_directories(out);out=std::filesystem::absolute(out);
    auto executableRoot=std::filesystem::absolute(argv[0]).parent_path();
#ifdef _WIN32
    std::array<wchar_t,32768> modulePath{};DWORD length=GetModuleFileNameW(nullptr,modulePath.data(),DWORD(modulePath.size()));
    if(!length||length>=modulePath.size())throw std::runtime_error("cannot resolve profiler executable path");
    executableRoot=std::filesystem::path(std::wstring(modulePath.data(),length)).parent_path();
#endif
    HardwareTelemetry hardware;hardware.start();
    std::ofstream metadata(out/"manifest.json");metadata<<"{\"seed\":\""<<seed<<"\",\"compiler\":\""<<__VERSION__<<"\",\"build_type\":\""<<BLOXIDE_BENCHMARK_BUILD_TYPE<<"\",\"optimization\":"<<BLOXIDE_BENCHMARK_OPT_LEVEL
        <<",\"backend\":\""<<backendText<<"\",\"quick\":"<<(quick?"true":"false")<<",\"runs\":[";
    if(!metadata)throw std::runtime_error("cannot open benchmark manifest");
    bool manifestComplete=false;
    struct PartialManifest {std::ofstream&out;bool&complete;~PartialManifest(){if(!complete)out<<"],\"suite_complete\":false}\n";}} partial{metadata,manifestComplete};
    bool first=true;auto result=[&](const std::string&name,const std::filesystem::path&file){if(!first)metadata<<',';first=false;metadata<<"{\"name\":\""<<name<<"\",\"file\":\""<<std::filesystem::relative(file,out).generic_string()<<"\"}";};
    if(suite=="noise"||suite=="all"){
        auto root=out/"noise";std::filesystem::create_directories(root);
        std::vector<std::wstring> args{L"--seed",std::to_wstring(seed),L"--repeat",std::to_wstring(repeat),L"--output",root.wstring()};if(quick)args.push_back(L"--quick");
        if(launch(executableRoot/"BloxideNoiseLab.exe",args,root/"console.log",180000)!=0)throw std::runtime_error("noise suite failed: "+read(root/"console.log"));
        result("noise",root/"summary.json");
    }
    auto settings=WorldSettings::densityWorld();settings.seed=seed;
    settings.save(out/"worldgen-config.meta");
    if(suite=="worldgen"||suite=="all"){
        std::ofstream csv(out/"worldgen.csv");csv<<"repeat,chunk_x,chunk_z,reference_ms,optimized_ms,mesh_ms,hierarchy_ms,mesh_bytes,trees,villages,iron,gold,diamond,water,collapses,rejected\n";
        std::array<std::vector<double>,4> times;std::size_t maxMesh=0;
        ChunkGenerator gen(settings,backend);ChunkMesher mesher;
        for(unsigned r=0;r<repeat;++r)for(unsigned i=0;i<(quick?4U:32U);++i){
            ChunkCoord c{int(i)-8,int(r)-2};auto start=Clock::now();auto reference=gen.generate(c,1,nullptr,false);double refMs=milliseconds(start);
            start=Clock::now();auto data=gen.generate(c);double optMs=milliseconds(start);if(data->blocks!=reference->blocks)throw std::runtime_error("optimized/reference mismatch");
            times[0].push_back(refMs);times[1].push_back(optMs);
            ChunkNeighbors neighbors;neighbors.west=gen.generate({c.x-1,c.z});neighbors.east=gen.generate({c.x+1,c.z});neighbors.north=gen.generate({c.x,c.z-1});neighbors.south=gen.generate({c.x,c.z+1});
            start=Clock::now();auto mesh=mesher.build(c,data,neighbors,0);double meshMs=milliseconds(start);start=Clock::now();auto hierarchy=SurfaceHierarchy::build(*mesh,8);double hierarchyMs=milliseconds(start);
            times[2].push_back(meshMs);times[3].push_back(hierarchyMs);maxMesh=std::max(maxMesh,hierarchy->residentByteSize());
            csv<<r<<','<<c.x<<','<<c.z<<','<<refMs<<','<<optMs<<','<<meshMs<<','<<hierarchyMs<<','<<hierarchy->residentByteSize()<<','<<data->treeIncidences<<','<<data->villageIncidences
                <<','<<data->materialCounts[unsigned(BlockType::IronOre)]<<','<<data->materialCounts[unsigned(BlockType::GoldOre)]<<','<<data->materialCounts[unsigned(BlockType::DiamondOre)]<<','<<data->materialCounts[unsigned(BlockType::Water)]
                <<','<<hierarchy->collapseAccepted<<','<<hierarchy->collapseRejected<<'\n';
        }
        csv.flush();if(!csv)throw std::runtime_error("worldgen CSV write failed");
        std::ofstream summary(out/"worldgen-summary.json");summary<<std::setprecision(17)<<"{\"seed\":\""<<seed<<"\",\"samples\":"<<times[0].size()<<",\"max_mesh_payload_bytes\":"<<maxMesh<<",\"phases\":[";
        const char* names[]{"reference","optimized","meshing","simplification"};
        for(unsigned i=0;i<4;++i){if(i)summary<<',';summary<<"{\"name\":\""<<names[i]<<"\",\"p50_ms\":"<<percentile(times[i],50)<<",\"p95_ms\":"<<percentile(times[i],95)<<",\"p99_ms\":"<<percentile(times[i],99)<<'}';}
        summary<<"],\"headless_not_fps\":true}\n";if(!summary)throw std::runtime_error("worldgen summary write failed");
        result("worldgen",out/"worldgen-summary.json");
    }
    if(suite=="fluid"||suite=="all"){
        std::ofstream csv(out/"fluid.csv");csv<<"repeat,tick,frontier,updates,conversions,tick_ms\n";
        std::vector<double> times;std::size_t maxFrontier=0;std::uint64_t updateCount=0;
        for(unsigned r=0;r<repeat;++r){
            std::unordered_map<Int3,BlockState,VoxelCoordHash> grid;FluidEngine fluid;grid[{0,20,0}]=BlockState::water();grid[{2,20,0}]=BlockState::water();
            fluid.notify({0,20,0});fluid.notify({2,20,0});
            for(unsigned tick=0;tick<(quick?50U:300U);++tick){
                auto updates=fluid.tick([&](const Int3&p){return FluidCell{p.y==0?BlockState(BlockType::Stone):grid[p],true};});
                for(auto&u:updates)grid[u.position]=u.after;auto stats=fluid.stats();
                times.push_back(stats.lastMs);maxFrontier=std::max(maxFrontier,stats.frontier);updateCount+=updates.size();
                csv<<r<<','<<tick<<','<<stats.frontier<<','<<updates.size()<<','<<stats.sourceConversions<<','<<stats.lastMs<<'\n';
            }
        }
        csv.flush();if(!csv)throw std::runtime_error("fluid CSV write failed");
        std::ofstream summary(out/"fluid-summary.json");summary<<std::setprecision(17)<<"{\"ticks\":"<<times.size()<<",\"updates\":"<<updateCount<<",\"max_frontier\":"<<maxFrontier
            <<",\"p50_ms\":"<<percentile(times,50)<<",\"p95_ms\":"<<percentile(times,95)<<",\"p99_ms\":"<<percentile(times,99)<<",\"headless_not_fps\":true}\n";
        if(!summary)throw std::runtime_error("fluid summary write failed");result("fluid",out/"fluid-summary.json");
    }
    if(suite=="streaming"||suite=="render"||suite=="all"){
        std::vector<std::string> scenarios=suite=="streaming"?std::vector<std::string>{"cold","traversal","reversal","budget"}:std::vector<std::string>{"cold","steady","traversal","reversal","fluid","edit","budget","forest","village","underwater"};
        for(auto&scenario:scenarios)for(unsigned r=0;r<repeat;++r)for(bool overlay:{false,true}){
            auto root=out/(scenario+"-"+std::to_string(r)+(overlay?"-ui":"-plain"));std::filesystem::create_directories(root);
            if(scenario=="forest"||scenario=="village"){
                auto featureSettings=WorldSettings::densityWorld("flat");featureSettings.seed=seed;featureSettings.density.water=false;
                featureSettings.save(root/"world/world.meta");
            }
            std::vector<std::wstring> args{L"--benchmark-run",L"--seed",std::to_wstring(seed),L"--world",(root/"world").wstring(),
                L"--warmup",std::to_wstring(scenario=="cold"?0:warmup),L"--duration",std::to_wstring(duration),L"--scenario",std::wstring(scenario.begin(),scenario.end()),
                L"--benchmark-output",root.wstring(),L"--benchmark-devtools",overlay?L"on":L"off",L"--noise-backend",std::wstring(backendText.begin(),backendText.end())};
            if(!lod)args.push_back(L"--no-lod");
            if(scenario=="forest"||scenario=="village"){args.push_back(L"--preset");args.push_back(L"flat");args.push_back(L"--caves");args.push_back(L"off");}
            std::cout<<"runtime "<<scenario<<" repeat="<<r<<" UI="<<overlay<<std::endl;
            if(launch(executableRoot/"Bloxide.exe",args,root/"console.log",unsigned((duration+190)*1000))!=0)throw std::runtime_error("runtime suite failed: "+read(root/"console.log"));
            result(scenario+(overlay?" UI":" plain"),root/"runtime-summary.json");
        }
    }
    auto snapshot=hardware.snapshot();metadata<<"],\"process_working_set_bytes\":"<<snapshot.workingSet<<",\"physical_ram_bytes\":"<<snapshot.physicalTotal
        <<",\"cpu\":"<<std::quoted(snapshot.cpuName)<<",\"logical_cpus\":"<<snapshot.logicalCpus<<",\"suite_complete\":true}\n";
    manifestComplete=true;if(!metadata)throw std::runtime_error("benchmark manifest write failed");
    std::cout<<"Profiler suite complete: "<<out.string()<<std::endl;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
