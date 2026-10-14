#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
namespace bloxide {
struct HardwareSnapshot {
    std::string cpuName,gpuName,glVendor,glRenderer,glVersion;
    unsigned logicalCpus=0;
    bool processCpuAvailable=false,systemCpuAvailable=false,ramAvailable=false,gpuEngineAvailable=false,gpuMemoryAvailable=false;
    double processCpuPercent=0,systemCpuPercent=0,gpuBusiest3DPercent=0;
    std::uint64_t workingSet=0,privateCommit=0,physicalTotal=0,physicalAvailable=0,gpuLocalUsage=0,gpuLocalBudget=0,gpuSharedUsage=0;
    double sampledAt=0;
    std::string gpuEngineSource="N/A",gpuMemorySource="N/A";
};
class HardwareTelemetry {
public:
    ~HardwareTelemetry(){stop();}
    void start(std::string vendor={},std::string renderer={},std::string version={});
    void stop();
    HardwareSnapshot snapshot()const;
private:
    void run();
    std::atomic<bool> stopping_{false};
    std::thread thread_;
    mutable std::mutex mutex_;std::condition_variable cv_;
    HardwareSnapshot current_;
};
}
