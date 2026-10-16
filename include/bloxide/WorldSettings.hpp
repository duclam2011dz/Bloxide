#pragma once
#include <bloxide/Noise.hpp>
#include <filesystem>
#include <optional>
#include <string>
namespace bloxide {
struct OreSettings {
    int minY = 8, maxY = 128;
    double probability = 0.7;
    int minRadius = 1, maxRadius = 3;
};
struct DensitySettings {
    FractalSettings terrain{4, 1.0 / 96, 2, 0.5}, warp{3, 1.0 / 128, 2, 0.5},
        rockMask{3, 1.0 / 512, 2, 0.5};
    FractalSettings climate{3, 1.0 / 4096, 2, 0.5}, aquifer{3, 1.0 / 256, 2, 0.5};
    double amplitude = 1.4, warpStrength = 16, band = 56, baseScale = 24;
    int ceiling = 248, seaLevel = 62, waterTableBase = 32, waterTableAmplitude = 16;
    double aquiferThreshold = 0.6, caveDistributionMinimum = 0.25;
    bool water = true, ores = true, trees = true, villages = true, flowers = true;
    int oreCell = 16, treeCell = 8, villageCell = 512, villageJitter = 96;
    double villageProbability = 0.25;
    int villageMaxSlope = 6, villageMaxFoundation = 12;
    std::array<OreSettings, 3> ore{{{8, 128, 0.7, 1, 3}, {4, 48, 0.3, 1, 3}, {4, 24, 0.15, 1, 2}}};
    void validate() const;
};
struct WorldSettings {
    std::uint64_t seed = 130;
    std::string generatorVersion = "simplex-1", preset = "mountains-v1";
    FractalSettings continental{5, 1.0 / 1536, 2, 0.5}, ridges{5, 1.0 / 512, 2, 0.5};
    FractalSettings hills{4, 1.0 / 384, 2, 0.5}, turbulence{3, 1.0 / 256, 2, 0.5};
    FractalSettings warp2D{3, 1.0 / 1024, 2, 0.5}, caveWarp{3, 1.0 / 96, 2, 0.5},
        caveDensity{4, 1.0 / 48, 2, 0.5};
    double warpStrength = 64, caveWarpStrength = 12, caveThreshold = 0.38;
    bool caves = true;
    FractalSettings roughness{3, 1.0 / 768, 2, 0.5}, mountainMask{3, 1.0 / 1536, 2, 0.5};
    FractalSettings valley{4, 1.0 / 768, 2, 0.5}, plateau{3, 1.0 / 1024, 2, 0.5};
    std::array<double, 5> splineX{-1, -0.5, 0, 0.5, 1}, splineY{32, 48, 72, 96, 120};
    double maskLow = 0.45, maskHigh = 0.75, hillsAmplitude = 12, ridgeAmplitude = 84,
           valleyAmplitude = 24;
    double plateauBlend = 0.65, plateauHeight = 144, minHeight = 24, maxHeight = 224;
    static WorldSettings terrainFields(std::string preset = "mountainous");
    DensitySettings density{};
    static WorldSettings densityWorld(std::string preset = "mountainous");
    void validate() const;
    static std::uint64_t parseSeed(const std::string& text);
    static WorldSettings open(const std::filesystem::path& directory,
                              std::optional<std::uint64_t> requested = {},
                              std::optional<std::string> preset = {},
                              std::optional<bool> caves = {});
    static WorldSettings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};
} // namespace bloxide
