#include "bloxide/Profiler.hpp"
#include <algorithm>
#include <fstream>

namespace bloxide {
void Profiler::beginFrame() { frameStart_ = Clock::now(); current_ = {}; current_.frame = history_.size(); }
void Profiler::beginPhase(const std::string& name) { starts_[name] = Clock::now(); }
void Profiler::endPhase(const std::string& name) { const auto it = starts_.find(name); if (it != starts_.end()) { current_.phases[name] += std::chrono::duration<double, std::milli>(Clock::now() - it->second).count(); starts_.erase(it); } }
void Profiler::endFrame() { current_.frameMs = std::chrono::duration<double, std::milli>(Clock::now() - frameStart_).count(); history_.push_back(current_); }
double Profiler::phaseMs(const std::string& name) const { const auto it = current_.phases.find(name); return it == current_.phases.end() ? 0.0 : it->second; }
void Profiler::clearHistory() { history_.clear(); }

bool Profiler::writeCsv(const std::string& path) const {
    std::ofstream out(path); if (!out) return false;
    out << "frame,frame_ms,generation_ms,meshing_ms,upload_ms,render_ms,other_ms\n";
    for (const auto& frame : history_) {
        const auto get = [&](const char* key) { const auto it = frame.phases.find(key); return it == frame.phases.end() ? 0.0 : it->second; };
        const double known = get("generation") + get("meshing") + get("upload") + get("render");
        out << frame.frame << ',' << frame.frameMs << ',' << get("generation") << ',' << get("meshing") << ',' << get("upload") << ',' << get("render") << ',' << std::max(0.0, frame.frameMs - known) << '\n';
    }
    return true;
}

bool Profiler::writeJson(const std::string& path) const {
    std::ofstream out(path); if (!out) return false;
    out << "{\"frames\":[";
    for (std::size_t i = 0; i < history_.size(); ++i) {
        const auto& frame = history_[i]; if (i) out << ',';
        out << "{\"frame\":" << frame.frame << ",\"frame_ms\":" << frame.frameMs << ",\"phases\":{";
        std::size_t n = 0; for (const auto& [name, value] : frame.phases) { if (n++) out << ','; out << '\"' << name << "\":" << value; }
        out << "}}";
    }
    out << "]}\n"; return true;
}

bool Profiler::writeTrace(const std::string& path) const {
    std::ofstream out(path); if (!out) return false;
    out << "{\"traceEvents\":["; bool first = true; double timestamp = 0.0;
    for (const auto& frame : history_) for (const auto& [name, value] : frame.phases) { if (!first) out << ','; first = false; out << "{\"name\":\"" << name << "\",\"cat\":\"bloxide\",\"ph\":\"X\",\"ts\":" << timestamp * 1000.0 << ",\"dur\":" << value * 1000.0 << ",\"pid\":1,\"tid\":1}"; timestamp += frame.frameMs; }
    out << "]}\n"; return true;
}
} // namespace bloxide
