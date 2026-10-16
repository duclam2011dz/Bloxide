#include <bloxide/BuildInfo.hpp>
#include <bloxide/RuntimePaths.hpp>
#include <bloxide/World.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
int main(int argc, char** argv) {
    if (bloxide::printVersionIfRequested(argc, argv))
        return 0;
    auto utf8Arguments = bloxide::platformArguments(argc, argv);
    std::vector<char*> utf8Pointers;
    for (auto& value : utf8Arguments)
        utf8Pointers.push_back(value.data());
    argc = int(utf8Pointers.size());
    argv = utf8Pointers.data();
    using Clock = std::chrono::steady_clock;
    bool quick = argc > 1 && std::string(argv[1]) == "--quick";
    auto outputRoot = bloxide::diagnosticDirectory("legacy");
    std::filesystem::create_directories(outputRoot);
    std::ofstream csv(outputRoot / "v1.2-streaming.csv");
    std::ofstream trace(outputRoot / "v1.2-trace.json");
    trace << "{\"traceEvents\":[";
    bool firstTrace = true;
    const auto epoch = Clock::now();
    csv << "scenario,sample,driver_ms,loaded,ready,generation_jobs,meshing_jobs,swaps,stale_"
           "results,edit_publish_ms\n";
    bloxide::World world;
    std::array<float, 3> pos{8, 80, 8};
    std::vector<double> timings, latencies;
    auto tick = [&](const char* scenario, int frame) {
        auto start = Clock::now();
        world.update(pos, {0, 0, -1});
        // CPU lifecycle benchmark: simulated acknowledgement, no GPU upload.
        for (auto& u : world.takePendingUploads())
            world.markUploaded(u);
        for (auto& g : world.readyGroups())
            world.commitGroup(g);
        auto s = world.stats();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (!firstTrace)
            trace << ',';
        firstTrace = false;
        trace << "{\"name\":\"" << scenario << "\",\"cat\":\"cpu_driver\",\"ph\":\"X\",\"ts\":"
              << std::chrono::duration<double, std::micro>(start - epoch).count()
              << ",\"dur\":" << ms * 1000 << ",\"pid\":1,\"tid\":1}";
        timings.push_back(ms);
        csv << scenario << ',' << frame << ',' << ms << ',' << s.loaded << ',' << s.ready << ','
            << s.generationJobs << ',' << s.meshingJobs << ',' << s.swaps << ',' << s.staleResults
            << ',' << s.lastPublishMs << '\n';
        return s;
    };
    auto deadline = Clock::now() + std::chrono::seconds(quick ? 10 : 30);
    int frames = 0;
    while (Clock::now() < deadline) {
        auto s = tick("cold", frames++);
        if (s.ready >= 289)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (world.stats().ready < 289) {
        std::cerr << "cold stream timeout\n";
        return 1;
    }
    for (const char* scenario : {"steady", "traversal", "edit"}) {
        const int count = quick ? 12 : 120;
        for (int i = 0; i < count; ++i) {
            if (std::string(scenario) == "traversal")
                pos[0] += quick ? 2.0f : 0.5f;
            tick(scenario, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (std::string(scenario) == "edit") {
            int x = static_cast<int>(pos[0]), z = static_cast<int>(pos[2]);
            deadline = Clock::now() + std::chrono::seconds(15);
            while (!world.targetable(x, z) && Clock::now() < deadline) {
                tick("settle", 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            int h = bloxide::ChunkGenerator::surfaceHeight(x, z);
            for (int edit = 0; edit < (quick ? 2 : 10); ++edit) {
                if (!world.set(x, h, z,
                               edit % 2 ? bloxide::BlockType::Grass : bloxide::BlockType::Air))
                    return 2;
                deadline = Clock::now() + std::chrono::seconds(15);
                while (!world.targetable(x, z) && Clock::now() < deadline) {
                    tick("edit_wait", edit);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (!world.targetable(x, z))
                    return 3;
                latencies.push_back(world.stats().lastPublishMs);
            }
        }
    }
    std::sort(timings.begin(), timings.end());
    std::sort(latencies.begin(), latencies.end());
    auto quantile = [](const std::vector<double>& v, double q) {
        return v.empty() ? 0.0
                         : v[std::min(v.size() - 1, static_cast<std::size_t>(q * (v.size() - 1)))];
    };
    auto s = world.stats();
    std::ofstream json(outputRoot / "v1.2-summary.json");
    json << "{\"headless\":true,\"samples\":" << timings.size()
         << ",\"driver_p95_ms\":" << quantile(timings, 0.95)
         << ",\"driver_max_ms\":" << timings.back()
         << ",\"edit_publish_p95_ms\":" << quantile(latencies, 0.95)
         << ",\"edit_publish_max_ms\":" << quantile(latencies, 1) << ",\"loaded\":" << s.loaded
         << ",\"ready\":" << s.ready << ",\"swaps\":" << s.swaps
         << ",\"stale_results\":" << s.staleResults << "}\n";
    std::cout << "samples=" << timings.size() << " driver_p95_ms=" << quantile(timings, 0.95)
              << " driver_max_ms=" << timings.back()
              << " edit_publish_p95_ms=" << quantile(latencies, 0.95)
              << " edit_publish_max_ms=" << quantile(latencies, 1) << " swaps=" << s.swaps
              << " stale_results=" << s.staleResults << std::endl;
    trace << "]}\n";
    return csv && json && trace ? 0 : 4;
}
