#include "bloxide/Profiler.hpp"
#include "bloxide/World.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
    const int warmupMs = quick ? 50 : 1000;
    const int sampleMs = quick ? 250 : 3000;
    std::filesystem::create_directories("build/benchmark");
    bloxide::World world;
    bloxide::Profiler profiler;
    const std::vector<std::array<float, 3>> positions{{8.0f, 67.5f, 8.0f}, {72.0f, 67.5f, 8.0f}, {136.0f, 67.5f, 72.0f}, {8.0f, 67.5f, 8.0f}};
    for (std::size_t scenario = 0; scenario < positions.size(); ++scenario) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < warmupMs) {
            profiler.beginFrame(); profiler.beginPhase("generation"); world.update(positions[scenario], {0.0f, 0.0f, -1.0f}); profiler.endPhase("generation"); profiler.endFrame(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto sampleStart = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sampleStart).count() < sampleMs) {
            profiler.beginFrame(); profiler.beginPhase("generation"); world.update(positions[scenario], {0.0f, 0.0f, -1.0f}); profiler.endPhase("generation"); profiler.endFrame(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto stats = world.stats();
        std::cout << "scenario=" << scenario << " loaded=" << stats.loaded << " generated=" << stats.generationJobs << " meshed=" << stats.meshingJobs << " visible=" << stats.visible << "\n";
    }
    profiler.writeCsv("build/benchmark/v1.1-runtime.csv");
    profiler.writeJson("build/benchmark/v1.1-summary.json");
    profiler.writeTrace("build/benchmark/v1.1-trace.json");
    std::ofstream summary("build/benchmark/v1.1-metrics.txt");
    const auto stats = world.stats();
    summary << "loaded=" << stats.loaded << "\n" << "generated_jobs=" << stats.generationJobs << "\n" << "meshing_jobs=" << stats.meshingJobs << "\n" << "cancelled_jobs=" << stats.cancelledJobs << "\n" << "visible=" << stats.visible << "\n";
    return 0;
}
