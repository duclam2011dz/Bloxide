#pragma once
#include <bloxide/ChunkGenerator.hpp>
#include <vector>
namespace bloxide {
enum class Biome : unsigned char { Plains, Forest, Mountains };
struct ClimateSample {
    double temperature = 0.5, humidity = 0.5;
    Biome biome = Biome::Plains;
};
struct FeatureBounds {
    std::int64_t minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    int minY = 0, maxY = 0;
};
struct HouseDescriptor {
    std::int64_t x = 0, z = 0;
    int y = 0, width = 7, depth = 9;
};
struct VillageDescriptor {
    std::uint64_t id = 0;
    std::int64_t x = 0, z = 0;
    int y = 0;
    FeatureBounds bounds{};
    std::vector<HouseDescriptor> houses;
};
struct TreeDescriptor {
    std::uint64_t id = 0;
    std::int64_t x = 0, z = 0;
    int y = 0, height = 0, species = 0;
    FeatureBounds bounds{};
};
struct DensityNode {
    double terrain = 0, cave = 0;
};
class DensityWorld {
  public:
    explicit DensityWorld(const WorldSettings&, NoiseBackend backend = NoiseBackend::Auto);
    ClimateSample climate(std::int64_t x, std::int64_t z) const;
    DensityNode node(std::int64_t x, int y, std::int64_t z) const;
    DensityNode fields(std::int64_t x, int y, std::int64_t z) const;
    double density(std::int64_t x, int y, std::int64_t z) const;
    int surfaceAt(std::int64_t x, std::int64_t z) const;
    BlockState baseState(std::int64_t x, int y, std::int64_t z) const;
    std::vector<VillageDescriptor> villagesIn(FeatureBounds) const;
    std::vector<TreeDescriptor> treesIn(FeatureBounds) const;
    BlockState sampleState(std::int64_t x, int y, std::int64_t z) const;
    std::shared_ptr<ChunkData> generate(ChunkCoord, std::uint64_t, const std::atomic<bool>*,
                                        bool optimized = true) const;
    const WorldSettings& settings() const {
        return settings_;
    }
    NoiseBackend backend() const {
        return backend_;
    }

  private:
    bool solid(std::int64_t x, int y, std::int64_t z) const;
    VillageDescriptor village(std::int64_t cellX, std::int64_t cellZ) const;
    TreeDescriptor tree(std::int64_t cellX, std::int64_t cellZ) const;
    BlockType oreAt(std::int64_t x, int y, std::int64_t z) const;
    void stampOres(ChunkCoord, ChunkData&) const;
    BlockState stampVillage(const VillageDescriptor&, std::int64_t x, int y, std::int64_t z,
                            BlockState) const;
    BlockState stampTree(const TreeDescriptor&, std::int64_t x, int y, std::int64_t z,
                         BlockState) const;
    bool fillGrid(ChunkCoord, const std::atomic<bool>*, bool optimized) const;
    WorldSettings settings_;
    ChunkGenerator terrain_;
    SimplexNoise3D density_, wx_, wy_, wz_;
    SimplexNoise3D cave_, caveX_, caveY_, caveZ_;
    SimplexNoise2D rock_, temperature_, humidity_, aquifer_;
    FractalKernel densityKernel_, warpKernel_, rockKernel_, climateKernel_, aquiferKernel_;
    FractalKernel caveKernel_, caveWarpKernel_;
    NoiseBackend backend_;
    std::uint64_t contextId_;
};
} // namespace bloxide
