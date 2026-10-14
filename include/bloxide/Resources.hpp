#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <atomic>
namespace bloxide {
constexpr std::size_t MiB=1024*1024;
struct QueueCredits {
    explicit QueueCredits(std::size_t limit):capacity(limit){}
    std::atomic<std::size_t> used{0},capacity;
    bool acquire(){
        auto value=used.load(std::memory_order_relaxed);
        while(value<capacity.load(std::memory_order_relaxed))
            if(used.compare_exchange_weak(value,value+1,std::memory_order_acq_rel))return true;
        return false;
    }
    void release(){used.fetch_sub(1,std::memory_order_acq_rel);}
};
struct ResourceLimits {
    std::size_t cpuBytes=512*MiB,gpuBytes=128*MiB;
    std::size_t generationQueue=128,meshingQueue=64,uploadQueue=64;
    int requestedFarRadius=24;
    void validate()const{
        if(cpuBytes<64*MiB||cpuBytes>8192ULL*MiB||gpuBytes<16*MiB||gpuBytes>4096ULL*MiB||
            generationQueue<1||generationQueue>128||meshingQueue<1||meshingQueue>64||uploadQueue<1||uploadQueue>64||
            requestedFarRadius<8||requestedFarRadius>24)throw std::invalid_argument("invalid resource limits");
    }
};
class DistanceBudget {
public:
    int effective=24;
    void update(double dt,std::size_t cpu,std::size_t gpu,const ResourceLimits& limits,bool growthAllowed=true){
        effective=std::min(effective,limits.requestedFarRadius);
        bool pressure=cpu>limits.cpuBytes*9/10||gpu>limits.gpuBytes*9/10;
        if(pressure){effective=std::max(8,effective-1);headroom_=0;}
        else if(growthAllowed&&cpu<limits.cpuBytes*3/4&&gpu<limits.gpuBytes*3/4){
            headroom_+=std::max(0.0,dt);if(headroom_>=2){effective=std::min(limits.requestedFarRadius,effective+1);headroom_=0;}
        }else headroom_=0;
    }
private:double headroom_=0;
};
}
