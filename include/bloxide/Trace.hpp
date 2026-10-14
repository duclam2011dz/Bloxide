#pragma once
#include <bloxide/Chunk.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>
namespace bloxide {
enum class TracePhase:unsigned char {Generation,Meshing,FarGeneration,FarMeshing,Simplification,Fluid,Upload,Opaque,Cutout,Translucent,UI,WorldDriver,Other};
struct TraceEvent {double startUs=0,durationUs=0;std::uint32_t thread=0;TracePhase phase=TracePhase::Other;ChunkCoord chunk{};};
class TraceBuffer {
public:
    static TraceBuffer& instance();
    void setEnabled(bool enabled){enabled_=enabled;}
    bool enabled()const{return enabled_.load(std::memory_order_relaxed);}
    void clear();
    void push(TraceEvent);
    std::vector<TraceEvent> snapshot()const;
    std::uint64_t overwritten()const{return overwritten_.load();}
private:
    std::atomic<bool> enabled_{false};std::atomic<std::uint64_t> overwritten_{0};
    mutable std::mutex mutex_;std::deque<TraceEvent> events_;
};
class TraceScope {
public:
    explicit TraceScope(TracePhase phase,ChunkCoord coord={});
    ~TraceScope();
private:
    bool active_=false;TracePhase phase_;ChunkCoord coord_;
    std::chrono::steady_clock::time_point start_;
};
const char* tracePhaseName(TracePhase)noexcept;
}
