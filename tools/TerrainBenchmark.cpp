#include <bloxide/World.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <numeric>
using namespace bloxide;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
int main(int argc,char**argv){try{
    bool quick=false;for(int i=1;i<argc;++i){if(std::string(argv[i])=="--quick")quick=true;else throw std::invalid_argument("usage: BloxideTerrainBenchmark [--quick]");}
    std::filesystem::create_directories("build/benchmark");
    std::ofstream csv("build/benchmark/v1.3.5-terrain.csv"),json("build/benchmark/v1.3.5-summary.json");
    csv<<"metric,value,unit\n";json<<"{\n";bool first=true;
    auto metric=[&](const std::string&name,double value,const char*unit){
        csv<<name<<','<<value<<','<<unit<<'\n';if(!first)json<<",\n";first=false;json<<'"'<<name<<"\": "<<value;
        std::cout<<name<<'='<<value<<' '<<unit<<'\n';
    };
    auto settings=WorldSettings::terrainFields();settings.seed=130;ChunkGenerator gen(settings);
    int count=quick?4:32;volatile double sink=0;
    auto start=Clock::now();for(int i=0;i<(quick?1000:100000);++i)sink+=gen.sampleTerrain(i*13,-i*17).height;
    metric("height_sample",ms(start)*1e6/(quick?1000:100000),"ns/sample");
    for(bool optimized:{false,true}){
        start=Clock::now();for(int i=0;i<count;++i)sink+=gen.generate({i-8,3},1,nullptr,optimized)->sectionSolid[0];
        metric(optimized?"generation_optimized":"generation_reference",ms(start)/count,"ms/chunk");
    }
    for(int step:{2,4,8}){
        std::size_t bytes=0,triangles=0;start=Clock::now();
        for(int i=0;i<count;++i){HeightTileInput in;in.coord={i-8,3};in.step=step;auto mesh=HeightmapMesher::build(gen,in);bytes+=mesh->byteSize();triangles+=mesh->indices.size()/3;}
        metric("heightmap_step"+std::to_string(step),ms(start)/count,"ms/tile");
        metric("heightmap_bytes_step"+std::to_string(step),double(bytes)/count,"bytes/tile");
        metric("heightmap_triangles_step"+std::to_string(step),double(triangles)/count,"triangles/tile");
    }
    for(bool lod:{false,true}){
        World world(settings,lod);auto position=WorldPosition::fromAbsolute(8,128,8);
        std::vector<double> steady,driver;
        auto tick=[&]{auto t=Clock::now();world.updateAt(position,{0,0,-1});
            for(auto&u:world.takePendingUploads())world.markUploaded(u);
            for(auto&g:world.readyGroups())world.commitGroup(g);
            driver.push_back(ms(t));
        };
        auto settle=[&]{auto deadline=Clock::now()+std::chrono::seconds(60);
            for(;;){tick();auto active=world.renderChunksAt(position,{0,0,0});
                bool nearReady=true;
                for(int z=-8;z<=8;++z)for(int x=-8;x<=8;++x)nearReady&=world.targetable((position.chunk.x+x)*16,(position.chunk.z+z)*16);
                if(active.size()==(lod?2401:289)&&nearReady&&world.heightmapSettled())break;
                if(Clock::now()>deadline)throw std::runtime_error("benchmark settle timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        auto prefix=std::string(lod?"lod_on_":"lod_off_");
        start=Clock::now();settle();metric(prefix+"cold_stream",ms(start),"ms");
        std::size_t payload=0,triangles=0,farVertices=0;
        for(auto&r:world.renderChunksAt(position,{0,0,0})){payload+=r.mesh->byteSize();triangles+=r.mesh->indices.size()/3;farVertices+=r.mesh->heightVertices.size();}
        metric(prefix+"active_cpu_payload",double(payload),"bytes");
        metric(prefix+"triangles",double(triangles),"triangles");
        metric(prefix+"far_vertices",double(farVertices),"vertices");
        metric(prefix+"voxel_records",double(world.stats().loaded),"records");
        metric(prefix+"worker_generation_cpu",world.stats().generationCpuMs,"ms");
        metric(prefix+"worker_meshing_cpu",world.stats().meshingCpuMs,"ms");
        metric(prefix+"worker_heightmap_cpu",world.stats().heightmapCpuMs,"ms");
        for(int i=0;i<(quick?20:120);++i){auto t=Clock::now();tick();steady.push_back(ms(t));}
        std::sort(steady.begin(),steady.end());
        for(auto p:{50,95,99})metric(prefix+"steady_driver_p"+std::to_string(p),steady[(steady.size()-1)*p/100],"ms");
        start=Clock::now();
        for(int x:{16,32,16,0,-16,0}){position=WorldPosition::fromAbsolute(x+8,128,8);settle();}
        metric(prefix+"traversal_and_reversal",ms(start),"ms");
        start=Clock::now();
        for(int y=255;y>0;--y)if(isSolid(world.get(8,y,8))){world.set(8,y,8,BlockType::Air);break;}
        auto deadline=Clock::now()+std::chrono::seconds(30);
        do{tick();if(world.targetable(8,8))break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(Clock::now()<deadline);
        if(!world.targetable(8,8))throw std::runtime_error("edit timeout");
        metric(prefix+"edit_publish",ms(start),"ms");metric(prefix+"driver_max",*std::max_element(driver.begin(),driver.end()),"ms");
        metric(prefix+"stale_results",double(world.stats().staleResults),"results");
        metric(prefix+"heightmap_stale_results",double(world.stats().heightmapStale),"results");
    }
    json<<"\n}\n";std::cout<<"seed=130 checksum="<<sink<<" (headless: simulated upload, not FPS)\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
