#pragma once
#include <bloxide/Noise.hpp>
#include <bloxide/WorldSettings.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace bloxide {
struct BenchmarkRun {
    bool enabled = false, overlay = false;
    double warmup = 5, duration = 30;
    std::string scenario = "steady";
    std::filesystem::path output, diagnostics;
    NoiseBackend backend = NoiseBackend::Auto;
    int farRadius = 24;
};
struct ApplicationOptions {
    bool smoke = false, lod = true, requireLod = false, help = false, version = false, json = false,
         validateAssets = false;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> preset;
    std::optional<bool> caves;
    std::filesystem::path world;
    std::int64_t offset = 0;
    BenchmarkRun benchmark;
};
ApplicationOptions parseApplicationOptions(const std::vector<std::string>& args);
int runGameApplication(const WorldSettings& settings, const ApplicationOptions& options);
} // namespace bloxide
