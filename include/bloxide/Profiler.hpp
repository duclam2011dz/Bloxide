#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bloxide {

struct ProfileFrame {
    double frameMs = 0.0;
    std::unordered_map<std::string, double> phases;
    std::uint64_t frame = 0;
};

class Profiler {
public:
    void beginFrame();
    void beginPhase(const std::string& name);
    void endPhase(const std::string& name);
    void endFrame();
    [[nodiscard]] const ProfileFrame& current() const noexcept { return current_; }
    [[nodiscard]] double phaseMs(const std::string& name) const;
    void clearHistory();
    bool writeCsv(const std::string& path) const;
    bool writeJson(const std::string& path) const;
    bool writeTrace(const std::string& path) const;

private:
    using Clock = std::chrono::steady_clock;
    std::unordered_map<std::string, Clock::time_point> starts_;
    ProfileFrame current_;
    std::vector<ProfileFrame> history_;
    Clock::time_point frameStart_{};
};

} // namespace bloxide
