#pragma once
#include <bloxide/Chunk.hpp>
#include <bloxide/WorldSettings.hpp>
#include <memory>
namespace bloxide {
class DensityWorld;
struct TerrainSample {double height=64,continentalness=0,roughness=0,mountainMask=0,ridge=0,valley=0,plateau=0;};
class ChunkGenerator {
public:
    explicit ChunkGenerator(const WorldSettings& settings={},NoiseBackend backend=NoiseBackend::Auto);
    int heightAt(std::int64_t worldX,std::int64_t worldZ)const;
    TerrainSample sampleTerrain(std::int64_t worldX,std::int64_t worldZ,double footprint=1)const;
    double caveNode(std::int64_t worldX,int y,std::int64_t worldZ)const;
    std::shared_ptr<ChunkData> generate(ChunkCoord coord,std::uint64_t dataRevision=1,
        const std::atomic<bool>* cancellation=nullptr,bool optimized=true)const;
    const WorldSettings& settings()const{return settings_;}
    const DensityWorld* densityContext()const{return density_.get();}
    // Compatibility for small-coordinate fixtures, never used by the running world.
    static int surfaceHeight(std::int64_t x,std::int64_t z);
private:
    WorldSettings settings_;
    SimplexNoise2D continental_,ridges_,hills_,turbulence_,warpX_,warpZ_;
    SimplexNoise2D roughness_,mountainMask_,valley_,plateau_;
    SimplexNoise3D caves_,caveWarpX_,caveWarpY_,caveWarpZ_;
    std::shared_ptr<const DensityWorld> density_;
};
}
