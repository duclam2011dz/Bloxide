#pragma once
#include <bloxide/Noise.hpp>
#include <filesystem>
#include <optional>
#include <string>
namespace bloxide {
struct WorldSettings {
    std::uint64_t seed=130;
    std::string generatorVersion="simplex-1",preset="mountains-v1";
    FractalSettings continental{5,1.0/1536,2,0.5},ridges{5,1.0/512,2,0.5};
    FractalSettings hills{4,1.0/384,2,0.5},turbulence{3,1.0/256,2,0.5};
    FractalSettings warp2D{3,1.0/1024,2,0.5},caveWarp{3,1.0/96,2,0.5},caveDensity{4,1.0/48,2,0.5};
    double warpStrength=64,caveWarpStrength=12,caveThreshold=0.38;
    bool caves=true;
    FractalSettings roughness{3,1.0/768,2,0.5},mountainMask{3,1.0/1536,2,0.5};
    FractalSettings valley{4,1.0/768,2,0.5},plateau{3,1.0/1024,2,0.5};
    std::array<double,5> splineX{-1,-0.5,0,0.5,1},splineY{32,48,72,96,120};
    double maskLow=0.45,maskHigh=0.75,hillsAmplitude=12,ridgeAmplitude=84,valleyAmplitude=24;
    double plateauBlend=0.65,plateauHeight=144,minHeight=24,maxHeight=224;
    static WorldSettings terrainFields(std::string preset="mountainous");
    void validate()const;
    static std::uint64_t parseSeed(const std::string& text);
    static WorldSettings open(const std::filesystem::path& directory,std::optional<std::uint64_t> requested={},
        std::optional<std::string> preset={},std::optional<bool> caves={});
    static WorldSettings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file)const;
};
}
