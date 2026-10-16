#include <bloxide/Trace.hpp>
#include <thread>
namespace bloxide {
TraceBuffer& TraceBuffer::instance() {
    static TraceBuffer buffer;
    return buffer;
}
void TraceBuffer::clear() {
    std::lock_guard lock(mutex_);
    events_.clear();
    overwritten_ = 0;
}
void TraceBuffer::push(TraceEvent event) {
    if (!enabled())
        return;
    std::lock_guard lock(mutex_);
    events_.push_back(event);
    if (events_.size() > 65536) {
        events_.pop_front();
        ++overwritten_;
    }
}
std::vector<TraceEvent> TraceBuffer::snapshot() const {
    std::lock_guard lock(mutex_);
    return {events_.begin(), events_.end()};
}
TraceScope::TraceScope(TracePhase phase, ChunkCoord coord) : phase_(phase), coord_(coord) {
    active_ = TraceBuffer::instance().enabled();
    if (active_)
        start_ = std::chrono::steady_clock::now();
}
TraceScope::~TraceScope() {
    if (!active_)
        return;
    auto end = std::chrono::steady_clock::now();
    TraceEvent event;
    event.startUs = std::chrono::duration<double, std::micro>(start_.time_since_epoch()).count();
    event.durationUs = std::chrono::duration<double, std::micro>(end - start_).count();
    event.thread = std::uint32_t(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    event.phase = phase_;
    event.chunk = coord_;
    TraceBuffer::instance().push(event);
}
const char* tracePhaseName(TracePhase p) noexcept {
    constexpr const char* names[]{"Generation",
                                  "Meshing",
                                  "Far generation",
                                  "Far meshing",
                                  "QEM simplification",
                                  "Fluid tick",
                                  "GPU upload submission",
                                  "Opaque",
                                  "Cutout",
                                  "Translucent",
                                  "DevTools UI",
                                  "World driver",
                                  "Other"};
    auto i = unsigned(p);
    return i < std::size(names) ? names[i] : "Unknown";
}
} // namespace bloxide
