#pragma once
#include <bloxide/Chunk.hpp>
#include <bloxide/WorldSettings.hpp>
#include <memory>
namespace bloxide {
class ChunkGenerator {
public:
    explicit ChunkGenerator(const WorldSettings& settings={});
    int heightAt(std::int64_t worldX,std::int64_t worldZ)const;
    double caveNode(std::int64_t worldX,int y,std::int64_t worldZ)const;
    std::shared_ptr<ChunkData> generate(ChunkCoord coord,std::uint64_t dataRevision=1)const;
    const WorldSettings& settings()const{return settings_;}
    // Compatibility for small-coordinate fixtures, never used by the running world.
    static int surfaceHeight(std::int64_t x,std::int64_t z);
private:
    WorldSettings settings_;
    SimplexNoise2D continental_,ridges_,hills_,turbulence_,warpX_,warpZ_;
    SimplexNoise3D caves_,caveWarpX_,caveWarpY_,caveWarpZ_;
};
}
