#include <bloxide/Telemetry.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <cwchar>
#include <cctype>
#endif
namespace bloxide {
namespace {
using Clock=std::chrono::steady_clock;
double seconds(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
#ifdef _WIN32
std::uint64_t ticks(FILETIME value){ULARGE_INTEGER n{};n.LowPart=value.dwLowDateTime;n.HighPart=value.dwHighDateTime;return n.QuadPart;}
std::string utf8(const wchar_t* text){
    int count=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);if(count<=0)return {};
    std::string out(std::size_t(count),0);WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),count,nullptr,nullptr);out.pop_back();return out;
}
std::string normalized(std::string text){std::string out;for(unsigned char c:text)if(std::isalnum(c))out.push_back(char(std::tolower(c)));return out;}
struct GpuCounters {
    IDXGIAdapter3* adapter=nullptr;PDH_HQUERY query=nullptr;std::vector<PDH_HCOUNTER> engines;
    ~GpuCounters(){if(query)PdhCloseQuery(query);if(adapter)adapter->Release();}
    void initialize(HardwareSnapshot& sample){
        IDXGIFactory1* factory=nullptr;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return;
        std::vector<IDXGIAdapter1*> candidates;
        auto renderer=normalized(sample.glRenderer),vendor=normalized(sample.glVendor);
        for(UINT i=0;;++i){
            IDXGIAdapter1* next=nullptr;if(factory->EnumAdapters1(i,&next)==DXGI_ERROR_NOT_FOUND)break;
            if(!next)break;
            DXGI_ADAPTER_DESC1 description{};next->GetDesc1(&description);
            auto name=normalized(utf8(description.Description));
            std::string shortName=name;if(shortName.rfind("nvidia",0)==0)shortName.erase(0,6);
            if(shortName.rfind("amd",0)==0)shortName.erase(0,3);
            bool exact=!shortName.empty()&&renderer.find(shortName)!=std::string::npos;
            bool vendorMatch=(description.VendorId==0x10de&&vendor.find("nvidia")!=std::string::npos)||
                (description.VendorId==0x1002&&vendor.find("ati")!=std::string::npos)||(description.VendorId==0x8086&&vendor.find("intel")!=std::string::npos);
            if(exact||vendorMatch)candidates.push_back(next);else next->Release();
        }
        factory->Release();
        IDXGIAdapter1* selected=nullptr;
        for(auto* candidate:candidates){DXGI_ADAPTER_DESC1 d{};candidate->GetDesc1(&d);auto name=normalized(utf8(d.Description));if(name.rfind("nvidia",0)==0)name.erase(0,6);
            if(!name.empty()&&renderer.find(name)!=std::string::npos){if(selected){selected=nullptr;break;}selected=candidate;}
        }
        if(!selected&&candidates.size()==1)selected=candidates.front();
        DXGI_ADAPTER_DESC1 desc{};
        if(selected){selected->GetDesc1(&desc);sample.gpuName=utf8(desc.Description);selected->QueryInterface(IID_PPV_ARGS(&adapter));}
        for(auto* candidate:candidates)candidate->Release();
        if(!selected)return;
        // English object/counter may not exist on this OS/driver. Failure remains N/A.
        DWORD length=0;auto status=PdhExpandWildCardPathW(nullptr,L"\\GPU Engine(*)\\Utilization Percentage",nullptr,&length,0);
        if(status!=static_cast<PDH_STATUS>(PDH_MORE_DATA)||length>1024*1024)return;
        std::vector<wchar_t> paths(length);if(PdhExpandWildCardPathW(nullptr,L"\\GPU Engine(*)\\Utilization Percentage",paths.data(),&length,0)!=ERROR_SUCCESS)return;
        if(PdhOpenQueryW(nullptr,0,&query)!=ERROR_SUCCESS)return;
        std::wstring pid=L"pid_"+std::to_wstring(GetCurrentProcessId())+L"_";
        wchar_t luid[80]{};std::swprintf(luid,80,L"luid_0x%08x_0x%08x",unsigned(desc.AdapterLuid.HighPart),unsigned(desc.AdapterLuid.LowPart));
        for(const wchar_t* path=paths.data();*path;path+=std::wcslen(path)+1){
            std::wstring text(path);if(text.find(pid)==std::wstring::npos||text.find(luid)==std::wstring::npos||text.find(L"engtype_3D")==std::wstring::npos)continue;
            PDH_HCOUNTER counter=nullptr;
            if(engines.size()<64&&PdhAddEnglishCounterW(query,path,0,&counter)==ERROR_SUCCESS)engines.push_back(counter);
        }
        PdhCollectQueryData(query);
    }
    void sample(HardwareSnapshot& output){
        if(adapter){DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};
            if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local))&&
                SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared))){
                output.gpuMemoryAvailable=true;output.gpuLocalUsage=local.CurrentUsage;output.gpuLocalBudget=local.Budget;
                output.gpuSharedUsage=shared.CurrentUsage;output.gpuMemorySource="DXGI process usage / adapter budget";
            }
        }
        if(query&&!engines.empty()&&PdhCollectQueryData(query)==ERROR_SUCCESS){
            bool available=false;double busiest=0;
            for(auto counter:engines){PDH_FMT_COUNTERVALUE value{};DWORD type=0;
                if(PdhGetFormattedCounterValue(counter,PDH_FMT_DOUBLE,&type,&value)==ERROR_SUCCESS&&
                    (value.CStatus==PDH_CSTATUS_VALID_DATA||value.CStatus==PDH_CSTATUS_NEW_DATA)&&std::isfinite(value.doubleValue)){
                    available=true;busiest=std::max(busiest,value.doubleValue);
                }
            }
            output.gpuEngineAvailable=available;output.gpuBusiest3DPercent=std::clamp(busiest,0.0,100.0);
            if(available)output.gpuEngineSource="PDH: busiest matched process 3D engine";
        }
    }
};
#endif
}
void HardwareTelemetry::start(std::string vendor,std::string renderer,std::string version){
    stop();{std::lock_guard lock(mutex_);current_={};current_.glVendor=std::move(vendor);current_.glRenderer=std::move(renderer);current_.glVersion=std::move(version);}
    stopping_=false;thread_=std::thread(&HardwareTelemetry::run,this);
}
void HardwareTelemetry::stop(){stopping_=true;cv_.notify_all();if(thread_.joinable())thread_.join();}
HardwareSnapshot HardwareTelemetry::snapshot()const{std::lock_guard lock(mutex_);return current_;}
void HardwareTelemetry::run(){
    auto output=snapshot();
#ifdef _WIN32
    SYSTEM_INFO system{};GetSystemInfo(&system);output.logicalCpus=system.dwNumberOfProcessors;
    wchar_t cpu[256]{};DWORD bytes=sizeof(cpu);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,cpu,&bytes)==ERROR_SUCCESS)output.cpuName=utf8(cpu);
    GpuCounters gpu;gpu.initialize(output);
    std::uint64_t previousProcess=0,previousTotal=0,previousIdle=0;double previousWall=0;
#else
    output.logicalCpus=std::thread::hardware_concurrency();output.cpuName="OS telemetry unavailable on this platform";
#endif
    while(!stopping_){
        output.sampledAt=seconds();
#ifdef _WIN32
        FILETIME creation{},exit{},kernel{},user{},idle{},systemKernel{},systemUser{};
        if(GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)){
            auto now=ticks(kernel)+ticks(user);
            if(previousWall>0&&output.sampledAt>previousWall&&now>=previousProcess){
                output.processCpuAvailable=true;
                output.processCpuPercent=std::clamp(double(now-previousProcess)*1e-7/(output.sampledAt-previousWall)/std::max(1U,output.logicalCpus)*100,0.0,100.0);
            }previousProcess=now;previousWall=output.sampledAt;
        }
        if(GetSystemTimes(&idle,&systemKernel,&systemUser)){
            auto total=ticks(systemKernel)+ticks(systemUser),i=ticks(idle);
            if(previousTotal&&total>previousTotal&&i>=previousIdle){output.systemCpuAvailable=true;
                output.systemCpuPercent=std::clamp((1-double(i-previousIdle)/double(total-previousTotal))*100,0.0,100.0);}
            previousTotal=total;previousIdle=i;
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);MEMORYSTATUSEX physical{};physical.dwLength=sizeof(physical);
        if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))&&GlobalMemoryStatusEx(&physical)){
            output.ramAvailable=true;output.workingSet=memory.WorkingSetSize;output.privateCommit=memory.PrivateUsage;
            output.physicalTotal=physical.ullTotalPhys;output.physicalAvailable=physical.ullAvailPhys;
        }
        gpu.sample(output);
#endif
        {std::lock_guard lock(mutex_);current_=output;}
        std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::seconds(1),[&]{return stopping_.load();});
    }
}
}
