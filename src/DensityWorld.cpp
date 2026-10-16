#include <bloxide/DensityWorld.hpp>
#include <bloxide/Coordinates.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
namespace bloxide {
namespace {
std::atomic<std::uint64_t> nextContext{1};
WorldSettings heightSettings(WorldSettings s) {
    s.generatorVersion = "terrain-fields-1";
    return s;
}
std::int64_t floorDivide(std::int64_t x, int n) {
    auto q = x / n;
    return q - (x % n < 0);
}
double smooth(double a, double b, double x) {
    x = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double unit(std::uint64_t h) {
    return double(h >> 11) * (1.0 / 9007199254740992.0);
}
double densitySlope(double dx, double dy, double dz) {
    return dy < -1e-8 ? std::hypot(dx, dz) / (-dy) : std::numeric_limits<double>::infinity();
}
bool contains(const FeatureBounds& b, std::int64_t x, int y, std::int64_t z) {
    return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ && y >= b.minY && y <= b.maxY;
}
bool intersects(const FeatureBounds& a, const FeatureBounds& b) {
    return a.minX <= b.maxX && a.maxX >= b.minX && a.minZ <= b.maxZ && a.maxZ >= b.minZ &&
           a.minY <= b.maxY && a.maxY >= b.minY;
}
void checkCoordinates(std::int64_t x, std::int64_t z) {
    if (x < -WorldLimit - 256 || x > WorldLimit + 256 || z < -WorldLimit - 256 ||
        z > WorldLimit + 256)
        throw std::out_of_range("density coordinates outside world apron");
}
struct NodeEntry {
    std::int64_t x = 0, z = 0;
    int y = 0;
    bool valid = false;
    DensityNode value{};
};
struct SurfaceEntry {
    std::int64_t x = 0, z = 0;
    int y = 0;
    bool valid = false;
};
struct VillageEntry {
    std::int64_t x = 0, z = 0;
    bool valid = false;
    VillageDescriptor value{};
};
struct Scratch {
    std::uint64_t context = 0;
    std::array<NodeEntry, 8192> nodes{};
    std::array<SurfaceEntry, 2048> surfaces{};
    std::array<VillageEntry, 64> villages{};
    std::array<DensityNode, 7 * 7 * 65> grid{};
    std::array<BlockState, 18 * 18 * 256> blocks{};
    std::array<int, 18 * 18> tops{};
    std::array<double, 7 * 7 * 65> x{}, y{}, z{}, wx{}, wy{}, wz{}, values{};
    std::array<unsigned, 7 * 7 * 65> pointIndices{};
};
thread_local Scratch scratch;
Scratch& local(std::uint64_t context) {
    if (scratch.context != context) {
        scratch.context = context;
        for (auto& e : scratch.nodes)
            e.valid = false;
        for (auto& e : scratch.surfaces)
            e.valid = false;
        for (auto& e : scratch.villages)
            e.valid = false;
    }
    return scratch;
}
DensityNode interpolate(const std::array<DensityNode, 8>& nodes, double fx, double fy, double fz) {
    DensityNode value;
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx) {
                auto n = nodes[(dy * 2 + dz) * 2 + dx];
                double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
                value.terrain += n.terrain * weight;
                value.cave += n.cave * weight;
            }
    return value;
}
} // namespace
DensityWorld::DensityWorld(const WorldSettings& s, NoiseBackend backend)
    : settings_(s), terrain_(heightSettings(s)),
      density_(SeedDerivation::derive(s.seed, SeedDomain::Density)),
      wx_(SeedDerivation::derive(s.seed, SeedDomain::DensityWarpX)),
      wy_(SeedDerivation::derive(s.seed, SeedDomain::DensityWarpY)),
      wz_(SeedDerivation::derive(s.seed, SeedDomain::DensityWarpZ)),
      cave_(SeedDerivation::derive(s.seed, SeedDomain::Caves)),
      caveX_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpX)),
      caveY_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpY)),
      caveZ_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpZ)),
      rock_(SeedDerivation::derive(s.seed, SeedDomain::RockMask)),
      temperature_(SeedDerivation::derive(s.seed, SeedDomain::Temperature)),
      humidity_(SeedDerivation::derive(s.seed, SeedDomain::Humidity)),
      aquifer_(SeedDerivation::derive(s.seed, SeedDomain::Aquifer)),
      densityKernel_(s.density.terrain), warpKernel_(s.density.warp),
      rockKernel_(s.density.rockMask), climateKernel_(s.density.climate),
      aquiferKernel_(s.density.aquifer), caveKernel_(s.caveDensity), caveWarpKernel_(s.caveWarp),
      backend_(backend), contextId_(nextContext.fetch_add(1)) {
    s.validate();
    if (!noiseBackendSupported(backend))
        throw std::invalid_argument("density backend unsupported");
}
ClimateSample DensityWorld::climate(std::int64_t x, std::int64_t z) const {
    checkCoordinates(x, z);
    ClimateSample c;
    c.temperature = (climateKernel_.sample2D(temperature_, double(x), double(z)) + 1) * 0.5;
    c.humidity = (climateKernel_.sample2D(humidity_, double(x), double(z)) + 1) * 0.5;
    auto mountain = terrain_.sampleTerrain(x, z).mountainMask;
    c.biome = mountain >= 0.6      ? Biome::Mountains
              : c.humidity >= 0.55 ? Biome::Forest
                                   : Biome::Plains;
    return c;
}
DensityNode DensityWorld::node(std::int64_t x, int y, std::int64_t z) const {
    checkCoordinates(x, z);
    if (y < 0)
        return {-1, -1};
    if (y <= 3)
        return {1, -1};
    if (y >= settings_.density.ceiling)
        return {-1, -1};
    auto& cache = local(contextId_);
    auto hash = SeedDerivation::coordinateHash(0, x, y, z);
    auto& e = cache.nodes[hash & (cache.nodes.size() - 1)];
    if (e.valid && e.x == x && e.y == y && e.z == z)
        return e.value;
    auto h = terrain_.sampleTerrain(x, z).height;
    auto& config = settings_.density;
    double d = (h - y) / config.baseScale;
    double band = 1 - smooth(config.band * 0.65, config.band, std::abs(y - h));
    if (config.amplitude > 0 && band > 0) {
        double a = double(x), b = y, c = double(z);
        double xx =
            a + config.warpStrength * warpKernel_.sample3D(wx_, a, b, c, FractalMode::Fbm, 4);
        double yy =
            b + config.warpStrength * warpKernel_.sample3D(wy_, a, b, c, FractalMode::Fbm, 4);
        double zz =
            c + config.warpStrength * warpKernel_.sample3D(wz_, a, b, c, FractalMode::Fbm, 4);
        double mask = smooth(0.35, 0.65, (rockKernel_.sample2D(rock_, a, c) + 1) * 0.5);
        d += config.amplitude * band * mask *
             densityKernel_.sample3D(density_, xx, yy, zz, FractalMode::Fbm, 4);
    }
    double cave = -1;
    if (settings_.caves && y <= h + config.band) {
        double humidity = (climateKernel_.sample2D(humidity_, double(x), double(z)) + 1) * 0.5;
        double distribution =
            config.caveDistributionMinimum + (1 - config.caveDistributionMinimum) * humidity;
        cave = terrain_.caveNode(x, y, z) * smooth(4, 12, y) * distribution;
    }
    e = {x, z, y, true, {d, cave}};
    return e.value;
}
DensityNode DensityWorld::fields(std::int64_t x, int y, std::int64_t z) const {
    checkCoordinates(x, z);
    if (y < 0 || y >= settings_.density.ceiling)
        return {-1, -1};
    if (y <= 3)
        return {1, -1};
    auto gx = floorDivide(x, 4) * 4, gz = floorDivide(z, 4) * 4;
    int gy = y / 4 * 4;
    std::array<DensityNode, 8> nodes;
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx)
                nodes[(dy * 2 + dz) * 2 + dx] = node(gx + 4 * dx, gy + 4 * dy, gz + 4 * dz);
    return interpolate(nodes, double(x - gx) / 4, double(y - gy) / 4, double(z - gz) / 4);
}
double DensityWorld::density(std::int64_t x, int y, std::int64_t z) const {
    auto n = fields(x, y, z);
    return n.cave > settings_.caveThreshold ? -std::max(0.001, n.cave - settings_.caveThreshold)
                                            : n.terrain;
}
bool DensityWorld::solid(std::int64_t x, int y, std::int64_t z) const {
    return y >= 0 && y < settings_.density.ceiling && density(x, y, z) >= 0;
}
int DensityWorld::surfaceAt(std::int64_t x, std::int64_t z) const {
    auto& cache = local(contextId_);
    auto& e =
        cache.surfaces[SeedDerivation::coordinateHash(0, x, 0, z) & (cache.surfaces.size() - 1)];
    if (e.valid && e.x == x && e.z == z)
        return e.y;
    int top =
        std::min(settings_.density.ceiling - 1,
                 int(std::ceil(terrain_.sampleTerrain(x, z).height + settings_.density.band)) + 4);
    while (top > 3 && !solid(x, top, z))
        --top;
    e = {x, z, top, true};
    return top;
}
BlockType DensityWorld::oreAt(std::int64_t x, int y, std::int64_t z) const {
    if (!settings_.density.ores)
        return BlockType::Stone;
    constexpr SeedDomain tags[]{SeedDomain::Iron, SeedDomain::Gold, SeedDomain::Diamond};
    constexpr BlockType types[]{BlockType::IronOre, BlockType::GoldOre, BlockType::DiamondOre};
    auto cx = floorDivide(x, 16), cz = floorDivide(z, 16);
    int cy = y / 16;
    for (int ore = 2; ore >= 0; --ore) {
        auto config = settings_.density.ore[ore];
        if (y < config.minY || y > config.maxY)
            continue;
        double depthWeight = ore == 0 ? 1
                                      : 0.35 + 0.65 * (config.maxY - y) /
                                                   double(std::max(1, config.maxY - config.minY));
        auto seed = SeedDerivation::derive(settings_.seed, tags[ore]);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    auto h = SeedDerivation::coordinateHash(seed, cx + dx, cy + dy, cz + dz);
                    if (unit(h) > config.probability * depthWeight)
                        continue;
                    SplitMix64 random(h);
                    auto px = (cx + dx) * 16 + 2 + std::int64_t(random.bounded(12)),
                         pz = (cz + dz) * 16 + 2 + std::int64_t(random.bounded(12));
                    int py = (cy + dy) * 16 + 2 + int(random.bounded(12));
                    if (py < config.minY || py > config.maxY)
                        continue;
                    double rx = config.minRadius +
                                random.bounded(unsigned(config.maxRadius - config.minRadius + 1));
                    double ry = config.minRadius +
                                random.bounded(unsigned(config.maxRadius - config.minRadius + 1));
                    double rz = config.minRadius +
                                random.bounded(unsigned(config.maxRadius - config.minRadius + 1));
                    double xx = double(x - px) / rx, yy = (y - py) / ry, zz = double(z - pz) / rz;
                    if (xx * xx + yy * yy + zz * zz <= 1)
                        return types[ore];
                }
    }
    return BlockType::Stone;
}
void DensityWorld::stampOres(ChunkCoord coord, ChunkData& data) const {
    if (!settings_.density.ores)
        return;
    constexpr SeedDomain tags[]{SeedDomain::Iron, SeedDomain::Gold, SeedDomain::Diamond};
    constexpr BlockType types[]{BlockType::IronOre, BlockType::GoldOre, BlockType::DiamondOre};
    auto bx = coord.x * 16, bz = coord.z * 16;
    // Candidate generation is per vein, not repeated for every Stone voxel.
    for (int ore = 2; ore >= 0; --ore) {
        auto config = settings_.density.ore[ore];
        auto seed = SeedDerivation::derive(settings_.seed, tags[ore]);
        for (int cy = config.minY / 16 - 1; cy <= config.maxY / 16 + 1; ++cy)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    auto cx = coord.x + dx, cz = coord.z + dz;
                    auto h = SeedDerivation::coordinateHash(seed, cx, cy, cz);
                    double probability = unit(h);
                    if (probability > config.probability)
                        continue;
                    SplitMix64 random(h);
                    auto px = cx * 16 + 2 + std::int64_t(random.bounded(12)),
                         pz = cz * 16 + 2 + std::int64_t(random.bounded(12));
                    int py = cy * 16 + 2 + int(random.bounded(12));
                    if (py < config.minY || py > config.maxY)
                        continue;
                    int rx = config.minRadius +
                             int(random.bounded(unsigned(config.maxRadius - config.minRadius + 1)));
                    int ry = config.minRadius +
                             int(random.bounded(unsigned(config.maxRadius - config.minRadius + 1)));
                    int rz = config.minRadius +
                             int(random.bounded(unsigned(config.maxRadius - config.minRadius + 1)));
                    for (auto z = std::max(bz, pz - rz); z <= std::min(bz + 15, pz + rz); ++z)
                        for (auto x = std::max(bx, px - rx); x <= std::min(bx + 15, px + rx); ++x)
                            for (int y = std::max(config.minY, py - ry);
                                 y <= std::min(config.maxY, py + ry); ++y) {
                                int lx = int(x - bx), lz = int(z - bz);
                                if (data.get(lx, y, lz) != BlockType::Stone ||
                                    y >= int(data.heightHalo[(lz + 1) * 18 + lx + 1]) - 3)
                                    continue;
                                double weight =
                                    ore == 0
                                        ? 1
                                        : 0.35 + 0.65 * (config.maxY - y) /
                                                     double(std::max(1, config.maxY - config.minY));
                                if (probability > config.probability * weight)
                                    continue;
                                double xx = double(x - px) / rx, yy = double(y - py) / ry,
                                       zz = double(z - pz) / rz;
                                if (xx * xx + yy * yy + zz * zz <= 1)
                                    data.blocks[data.index(lx, y, lz)] = types[ore];
                            }
                }
    }
}
BlockState DensityWorld::baseState(std::int64_t x, int y, std::int64_t z) const {
    if (y < 0 || y >= 256)
        return {};
    checkCoordinates(x, z);
    if (y == 0)
        return BlockType::Bedrock;
    if (y <= 3)
        return BlockType::Stone;
    int top = surfaceAt(x, z);
    if (solid(x, y, z)) {
        if (y >= top - 3) {
            double dx = fields(x + 1, top, z).terrain - fields(x - 1, top, z).terrain;
            double dy = fields(x, top + 1, z).terrain - fields(x, top - 1, z).terrain;
            double dz = fields(x, top, z + 1).terrain - fields(x, top, z - 1).terrain;
            if (densitySlope(dx, dy, dz) > 1)
                return BlockType::Stone;
            if (settings_.density.water && std::abs(top - settings_.density.seaLevel) <= 3)
                return BlockType::Sand;
            return y == top ? BlockType::Grass : BlockType::Dirt;
        }
        return oreAt(x, y, z);
    }
    if (settings_.density.water) {
        if (y > top && y <= settings_.density.seaLevel)
            return BlockState::water();
        double a = (aquiferKernel_.sample2D(aquifer_, double(x), double(z)) + 1) * 0.5;
        int table =
            std::min(settings_.density.seaLevel - 8,
                     settings_.density.waterTableBase +
                         int(std::floor((2 * a - 1) * settings_.density.waterTableAmplitude)));
        if (y <= table && a > settings_.density.aquiferThreshold)
            return BlockState::water();
    }
    return {};
}
VillageDescriptor DensityWorld::village(std::int64_t cx, std::int64_t cz) const {
    auto& cache = local(contextId_);
    auto& entry = cache.villages[SeedDerivation::coordinateHash(0, cx, 1, cz) & 63];
    if (entry.valid && entry.x == cx && entry.z == cz)
        return entry.value;
    VillageDescriptor v;
    auto h = SeedDerivation::coordinateHash(
        SeedDerivation::derive(settings_.seed, SeedDomain::Structures), cx, 0, cz);
    auto& config = settings_.density;
    if (config.villages && unit(h) < config.villageProbability) {
        SplitMix64 random(h);
        v.id = h;
        v.x = cx * config.villageCell + config.villageCell / 2 +
              std::int64_t(random.bounded(unsigned(2 * config.villageJitter + 1))) -
              config.villageJitter;
        v.z = cz * config.villageCell + config.villageCell / 2 +
              std::int64_t(random.bounded(unsigned(2 * config.villageJitter + 1))) -
              config.villageJitter;
        if (v.x >= -WorldLimit + 128 && v.x < WorldLimit - 128 && v.z >= -WorldLimit + 128 &&
            v.z < WorldLimit - 128) {
            v.y = surfaceAt(v.x, v.z) + 1;
            bool safe = v.y > config.seaLevel + 2 && v.y + 16 < config.ceiling;
            int count = 3 + int(random.bounded(4));
            int low = v.y, high = v.y;
            constexpr int slots[6][2]{{-24, -16}, {0, -24}, {24, -16},
                                      {24, 16},   {0, 24},  {-24, 16}};
            for (int i = 0; i < count && safe; ++i) {
                HouseDescriptor house;
                house.x = v.x + slots[i][0];
                house.z = v.z + slots[i][1];
                house.width = random.bounded(2) ? 7 : 9;
                house.depth = house.width + 2;
                house.y = surfaceAt(house.x, house.z) + 1;
                low = std::min(low, house.y);
                high = std::max(high, house.y);
                for (int z = 0; z < house.depth; z += house.depth - 1)
                    for (int x = 0; x < house.width; x += house.width - 1) {
                        int support = surfaceAt(house.x + x, house.z + z);
                        low = std::min(low, support + 1);
                        high = std::max(high, support + 1);
                        if (support <= config.seaLevel ||
                            support < house.y - config.villageMaxFoundation)
                            safe = false;
                        bool supported = false;
                        for (int depth = 1; depth <= config.villageMaxFoundation; ++depth)
                            if (solid(house.x + x, house.y - depth, house.z + z)) {
                                supported = true;
                                break;
                            }
                        safe &= supported;
                    }
                v.houses.push_back(house);
            }
            safe &= high - low <= config.villageMaxSlope;
            if (safe)
                v.bounds = {v.x - 36,
                            v.x + 40,
                            v.z - 36,
                            v.z + 40,
                            std::max(4, low - config.villageMaxFoundation),
                            high + 12};
            else
                v = {};
        } else
            v = {};
    }
    entry.x = cx;
    entry.z = cz;
    entry.valid = true;
    entry.value = v;
    return v;
}
std::vector<VillageDescriptor> DensityWorld::villagesIn(FeatureBounds b) const {
    std::vector<VillageDescriptor> out;
    if (!settings_.density.villages)
        return out;
    int cell = settings_.density.villageCell;
    for (auto z = floorDivide(b.minZ - 128, cell); z <= floorDivide(b.maxZ + 128, cell); ++z)
        for (auto x = floorDivide(b.minX - 128, cell); x <= floorDivide(b.maxX + 128, cell); ++x) {
            auto v = village(x, z);
            if (v.id && intersects(b, v.bounds))
                out.push_back(std::move(v));
        }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.id < b.id; });
    return out;
}
TreeDescriptor DensityWorld::tree(std::int64_t cx, std::int64_t cz) const {
    TreeDescriptor t;
    if (!settings_.density.trees)
        return t;
    int cell = settings_.density.treeCell;
    auto seed = SeedDerivation::derive(settings_.seed, SeedDomain::Vegetation),
         h = SeedDerivation::coordinateHash(seed, cx, 0, cz);
    t.x = cx * cell + std::int64_t((h >> 8) % unsigned(cell));
    t.z = cz * cell + std::int64_t((h >> 16) % unsigned(cell));
    if (t.x < -WorldLimit + 32 || t.x >= WorldLimit - 32 || t.z < -WorldLimit + 32 ||
        t.z >= WorldLimit - 32)
        return {};
    auto c = climate(t.x, t.z);
    double chance = c.biome == Biome::Forest ? 0.65 : c.biome == Biome::Mountains ? 0.16 : 0.12;
    if (unit(h) > chance)
        return {};
    // Stable priority/exclusion: no generation-order dependence.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dz) {
                auto other = SeedDerivation::coordinateHash(seed, cx + dx, 0, cz + dz);
                auto xx = (cx + dx) * cell + std::int64_t((other >> 8) % unsigned(cell)),
                     zz = (cz + dz) * cell + std::int64_t((other >> 16) % unsigned(cell));
                if (std::abs(xx - t.x) <= 5 && std::abs(zz - t.z) <= 5 && other < h)
                    return {};
            }
    t.y = surfaceAt(t.x, t.z) + 1;
    t.species = c.biome == Biome::Mountains ? 2 : c.humidity > 0.6 && c.temperature < 0.65 ? 1 : 0;
    t.height = (t.species == 2 ? 8 : 5) + int((h >> 24) % 4);
    if (t.y <= settings_.density.seaLevel + 1 || t.y + t.height + 3 >= settings_.density.ceiling)
        return {};
    if (baseState(t.x, t.y - 1, t.z) != BlockType::Grass)
        return {};
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (std::abs(surfaceAt(t.x + dx, t.z + dz) + 1 - t.y) > 2)
                return {};
    for (int d = 1; d <= 3; ++d)
        if (!solid(t.x, t.y - d, t.z))
            return {};
    t.bounds = {t.x - 4, t.x + 4, t.z - 4, t.z + 4, t.y, t.y + t.height + 2};
    for (auto& v : villagesIn(t.bounds))
        if (intersects(t.bounds, v.bounds))
            return {};
    t.id = h;
    return t;
}
std::vector<TreeDescriptor> DensityWorld::treesIn(FeatureBounds b) const {
    std::vector<TreeDescriptor> out;
    if (!settings_.density.trees)
        return out;
    int cell = settings_.density.treeCell;
    for (auto z = floorDivide(b.minZ - 4, cell); z <= floorDivide(b.maxZ + 4, cell); ++z)
        for (auto x = floorDivide(b.minX - 4, cell); x <= floorDivide(b.maxX + 4, cell); ++x) {
            auto t = tree(x, z);
            if (t.id && intersects(t.bounds, b))
                out.push_back(t);
        }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.id < b.id; });
    return out;
}
BlockState DensityWorld::stampTree(const TreeDescriptor& t, std::int64_t x, int y, std::int64_t z,
                                   BlockState original) const {
    if (!contains(t.bounds, x, y, z) || !isReplaceable(original.type()))
        return original;
    constexpr BlockType logs[]{BlockType::OakLog, BlockType::BirchLog, BlockType::PineLog},
        leaves[]{BlockType::OakLeaves, BlockType::BirchLeaves, BlockType::PineLeaves};
    int dy = y - t.y, dx = int(x - t.x), dz = int(z - t.z);
    if (dx == 0 && dz == 0 && dy < t.height)
        return logs[t.species];
    int radius = t.species == 2 ? std::max(0, 4 - (dy - (t.height - 6)) / 2) : 3;
    bool canopy =
        t.species == 2
            ? dy >= t.height - 6 && dy <= t.height + 1 && std::abs(dx) + std::abs(dz) <= radius + 1
            : dy >= t.height - 3 && dy <= t.height + 1 &&
                  dx * dx + dz * dz + (dy - t.height + 1) * (dy - t.height + 1) <= 10;
    return canopy ? BlockState(leaves[t.species]) : original;
}
BlockState DensityWorld::stampVillage(const VillageDescriptor& v, std::int64_t x, int y,
                                      std::int64_t z, BlockState original) const {
    if (!contains(v.bounds, x, y, z))
        return original;
    for (auto& h : v.houses)
        if (x >= h.x && x < h.x + h.width && z >= h.z && z < h.z + h.depth) {
            int lx = int(x - h.x), lz = int(z - h.z), dy = y - h.y;
            if (dy < 0) {
                if (y >= std::max(4, h.y - settings_.density.villageMaxFoundation))
                    return BlockType::Cobblestone;
            }
            if (dy == 0)
                return BlockType::OakPlanks;
            if (dy > 0 && dy < 5) {
                bool wall = lx == 0 || lz == 0 || lx == h.width - 1 || lz == h.depth - 1;
                bool door = lz == 0 && lx == h.width / 2 && dy <= 2;
                bool window = dy == 2 && (((lx == 0 || lx == h.width - 1) && lz == h.depth / 2) ||
                                          (lz == h.depth - 1 && lx == h.width / 2));
                if (wall && !door)
                    return window    ? BlockType::Glass
                           : dy == 1 ? BlockType::Bricks
                                     : BlockType::OakPlanks;
                if (dy == 1 && lx == 1 && lz == h.depth - 2)
                    return BlockType::BirchPlanks;
                if (dy == 1 && lx == h.width - 2 && lz == h.depth - 2)
                    return BlockType::Cobblestone;
                return BlockType::Air;
            }
            int roof = 5 + std::min(lx, h.width - 1 - lx) / 2;
            if (dy == roof)
                return BlockType::SlateRoof;
            if (dy >= 5 && dy < roof)
                return BlockType::Air;
        }
    bool road = false;
    int roadY = v.y;
    for (auto& h : v.houses) {
        auto xx = h.x + h.width / 2, zz = h.z - 2;
        if (x >= std::min(v.x, xx) - 1 && x <= std::max(v.x, xx) + 1 && std::abs(z - v.z) <= 1)
            road = true;
        if (std::abs(x - xx) <= 1 && z >= std::min(v.z, zz) - 1 && z <= std::max(v.z, zz) + 1)
            road = true;
    }
    if (road) {
        if (y == roadY - 1)
            return BlockType::Cobblestone;
        if (y >= std::max(4, roadY - settings_.density.villageMaxFoundation) && y < roadY - 1)
            return BlockType::Stone;
        if (y >= roadY && y < roadY + 3)
            return BlockType::Air;
    }
    return original;
}
BlockState DensityWorld::sampleState(std::int64_t x, int y, std::int64_t z) const {
    auto state = baseState(x, y, z);
    if (y <= 3 || y >= 248)
        return state;
    FeatureBounds point{x, x, z, z, y, y};
    auto villages = villagesIn(point);
    for (auto& v : villages)
        state = stampVillage(v, x, y, z, state);
    if (villages.empty())
        for (auto& t : treesIn(point))
            state = stampTree(t, x, y, z, state);
    if (settings_.density.flowers && state == BlockType::Air &&
        baseState(x, y - 1, z) == BlockType::Grass) {
        auto h = SeedDerivation::coordinateHash(
            SeedDerivation::derive(settings_.seed, SeedDomain::Flowers), x, 0, z);
        if (h % 97 == 0)
            state = (h & 256) ? BlockType::RedFlower : BlockType::YellowFlower;
    }
    return state;
}
bool DensityWorld::fillGrid(ChunkCoord c, const std::atomic<bool>* cancel, bool optimized) const {
    auto& s = local(contextId_);
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    auto bx = c.x * 16, bz = c.z * 16;
    auto& config = settings_.density;
    auto ni = [](int x, int y, int z) { return unsigned((y * 7 + z) * 7 + x); };
    if (!optimized) {
        for (int y = 0; y < 65; ++y) {
            if (cancelled())
                return false;
            for (int z = 0; z < 7; ++z)
                for (int x = 0; x < 7; ++x)
                    s.grid[ni(x, y, z)] = node(bx - 4 + x * 4, y * 4, bz - 4 + z * 4);
        }
        return true;
    }
    std::array<double, 49> heights{}, masks{}, humidity{};
    for (int z = 0; z < 7; ++z)
        for (int x = 0; x < 7; ++x) {
            auto wx = bx - 4 + x * 4, wz = bz - 4 + z * 4;
            unsigned column = unsigned(z * 7 + x);
            heights[column] = terrain_.sampleTerrain(wx, wz).height;
            masks[column] =
                smooth(0.35, 0.65, (rockKernel_.sample2D(rock_, double(wx), double(wz)) + 1) * 0.5);
            humidity[column] =
                (climateKernel_.sample2D(humidity_, double(wx), double(wz)) + 1) * 0.5;
        }
    std::size_t count = 0;
    for (int y = 0; y < 65; ++y) {
        if (cancelled())
            return false;
        for (int z = 0; z < 7; ++z)
            for (int x = 0; x < 7; ++x) {
                auto index = ni(x, y, z);
                int yy = y * 4;
                auto h = heights[z * 7 + x];
                s.grid[index] = {-1, -1};
                if (yy <= 3) {
                    s.grid[index].terrain = 1;
                    continue;
                }
                if (yy >= config.ceiling)
                    continue;
                s.grid[index].terrain = (h - yy) / config.baseScale;
                double band = 1 - smooth(config.band * 0.65, config.band, std::abs(yy - h));
                if (config.amplitude > 0 && band > 0) {
                    s.x[count] = double(bx - 4 + x * 4);
                    s.y[count] = yy;
                    s.z[count] = double(bz - 4 + z * 4);
                    s.pointIndices[count] = index;
                    ++count;
                }
            }
    }
    if (count) {
        warpKernel_.samplePoints3D(wx_, s.x.data(), s.y.data(), s.z.data(), count, s.wx.data(),
                                   s.wx.size(), FractalMode::Fbm, 4, backend_);
        warpKernel_.samplePoints3D(wy_, s.x.data(), s.y.data(), s.z.data(), count, s.wy.data(),
                                   s.wy.size(), FractalMode::Fbm, 4, backend_);
        warpKernel_.samplePoints3D(wz_, s.x.data(), s.y.data(), s.z.data(), count, s.wz.data(),
                                   s.wz.size(), FractalMode::Fbm, 4, backend_);
        for (std::size_t i = 0; i < count; ++i) {
            s.wx[i] = s.x[i] + config.warpStrength * s.wx[i];
            s.wy[i] = s.y[i] + config.warpStrength * s.wy[i];
            s.wz[i] = s.z[i] + config.warpStrength * s.wz[i];
        }
        densityKernel_.samplePoints3D(density_, s.wx.data(), s.wy.data(), s.wz.data(), count,
                                      s.values.data(), s.values.size(), FractalMode::Fbm, 4,
                                      backend_);
        for (std::size_t i = 0; i < count; ++i) {
            auto index = s.pointIndices[i], column = index % 49;
            double band =
                1 - smooth(config.band * 0.65, config.band, std::abs(s.y[i] - heights[column]));
            s.grid[index].terrain += config.amplitude * band * masks[column] * s.values[i];
        }
    }
    if (cancelled())
        return false;
    count = 0;
    if (settings_.caves)
        for (int y = 1; y < 65; ++y)
            for (int z = 0; z < 7; ++z)
                for (int x = 0; x < 7; ++x) {
                    int yy = y * 4;
                    if (yy >= config.ceiling || yy > heights[z * 7 + x] + config.band)
                        continue;
                    s.x[count] = double(bx - 4 + x * 4);
                    s.y[count] = yy;
                    s.z[count] = double(bz - 4 + z * 4);
                    s.pointIndices[count] = ni(x, y, z);
                    ++count;
                }
    if (count) {
        caveWarpKernel_.samplePoints3D(caveX_, s.x.data(), s.y.data(), s.z.data(), count,
                                       s.wx.data(), s.wx.size(), FractalMode::Fbm, 4, backend_);
        caveWarpKernel_.samplePoints3D(caveY_, s.x.data(), s.y.data(), s.z.data(), count,
                                       s.wy.data(), s.wy.size(), FractalMode::Fbm, 4, backend_);
        caveWarpKernel_.samplePoints3D(caveZ_, s.x.data(), s.y.data(), s.z.data(), count,
                                       s.wz.data(), s.wz.size(), FractalMode::Fbm, 4, backend_);
        for (std::size_t i = 0; i < count; ++i) {
            s.wx[i] = s.x[i] + settings_.caveWarpStrength * s.wx[i];
            s.wy[i] = s.y[i] + settings_.caveWarpStrength * s.wy[i];
            s.wz[i] = s.z[i] + settings_.caveWarpStrength * s.wz[i];
        }
        caveKernel_.samplePoints3D(cave_, s.wx.data(), s.wy.data(), s.wz.data(), count,
                                   s.values.data(), s.values.size(), FractalMode::Fbm, 4, backend_);
        for (std::size_t i = 0; i < count; ++i) {
            auto index = s.pointIndices[i], column = index % 49;
            double distribution = config.caveDistributionMinimum +
                                  (1 - config.caveDistributionMinimum) * humidity[column];
            s.grid[index].cave = s.values[i] * smooth(4, 12, s.y[i]) * distribution;
        }
    }
    return !cancelled();
}
std::shared_ptr<ChunkData> DensityWorld::generate(ChunkCoord c, std::uint64_t revision,
                                                  const std::atomic<bool>* cancel,
                                                  bool optimized) const {
    if (c.x < -WorldLimit / 16 - 16 || c.x > WorldLimit / 16 + 15 || c.z < -WorldLimit / 16 - 16 ||
        c.z > WorldLimit / 16 + 15)
        throw std::out_of_range("density chunk generation bounds");
    auto& s = local(contextId_);
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    auto bx = c.x * 16, bz = c.z * 16;
    auto idx = [](int x, int y, int z) { return (y * 18 + z) * 18 + x; };
    auto ni = [](int x, int y, int z) { return (y * 7 + z) * 7 + x; };
    if (!fillGrid(c, cancel, optimized))
        return {};
    s.blocks.fill(BlockType::Air);
    s.tops.fill(0);
    std::array<bool, 16> allAir{}, allSolid{};
    if (optimized)
        for (unsigned section = 0; section < 16; ++section) {
            bool air = true, solid = true;
            int end = std::min(248, int(section + 1) * 16);
            for (int gy = int(section) * 4; gy <= (end + 3) / 4; ++gy)
                for (int gz = 0; gz < 7; ++gz)
                    for (int gx = 0; gx < 7; ++gx) {
                        auto n = s.grid[ni(gx, gy, gz)];
                        air &= n.terrain < 0;
                        solid &= n.terrain >= 0 && n.cave <= settings_.caveThreshold;
                    }
            allAir[section] = air;
            allSolid[section] = solid;
        }
    for (int y = 0; y < 248; ++y) {
        if (cancelled())
            return {};
        if (y > 3 && allAir[y / 16])
            continue;
        for (int z = 0; z < 18; ++z)
            for (int x = 0; x < 18; ++x) {
                if (y <= 3 || allSolid[y / 16]) {
                    s.blocks[idx(x, y, z)] = y == 0 ? BlockType::Bedrock : BlockType::Stone;
                    s.tops[z * 18 + x] = y;
                    continue;
                }
                int lx = x - 1, lz = z - 1, gx = (lx + 4) / 4, gz = (lz + 4) / 4, gy = y / 4;
                std::array<DensityNode, 8> n;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dz = 0; dz < 2; ++dz)
                        for (int dx = 0; dx < 2; ++dx)
                            n[(dy * 2 + dz) * 2 + dx] = s.grid[ni(gx + dx, gy + dy, gz + dz)];
                auto d = interpolate(n, double((lx + 4) % 4) / 4, double(y % 4) / 4,
                                     double((lz + 4) % 4) / 4);
                if (y <= 3 || (d.terrain >= 0 && d.cave <= settings_.caveThreshold)) {
                    s.blocks[idx(x, y, z)] = y == 0 ? BlockType::Bedrock : BlockType::Stone;
                    s.tops[z * 18 + x] = y;
                }
            }
    }
    auto data = std::make_shared<ChunkData>();
    data->blocks.fill(BlockType::Air);
    auto terrainValue = [&](int lx, int y, int lz) {
        if (y < 0 || y >= settings_.density.ceiling)
            return -1.0;
        if (y <= 3)
            return 1.0;
        int gx = (lx + 4) / 4, gz = (lz + 4) / 4, gy = y / 4;
        std::array<DensityNode, 8> n;
        for (int dy = 0; dy < 2; ++dy)
            for (int dz = 0; dz < 2; ++dz)
                for (int dx = 0; dx < 2; ++dx)
                    n[(dy * 2 + dz) * 2 + dx] = s.grid[ni(gx + dx, gy + dy, gz + dz)];
        return interpolate(n, double((lx + 4) % 4) / 4, double(y % 4) / 4, double((lz + 4) % 4) / 4)
            .terrain;
    };
    for (int z = 0; z < 16; ++z) {
        if (cancelled())
            return {};
        for (int x = 0; x < 16; ++x) {
            int top = s.tops[(z + 1) * 18 + x + 1];
            auto wx = bx + x, wz = bz + z;
            auto& cache =
                s.surfaces[SeedDerivation::coordinateHash(0, wx, 0, wz) & (s.surfaces.size() - 1)];
            cache = {wx, wz, top, true};
            double slope = densitySlope(terrainValue(x + 1, top, z) - terrainValue(x - 1, top, z),
                                        terrainValue(x, top + 1, z) - terrainValue(x, top - 1, z),
                                        terrainValue(x, top, z + 1) - terrainValue(x, top, z - 1));
            double aquifer = (aquiferKernel_.sample2D(aquifer_, double(wx), double(wz)) + 1) * 0.5;
            int table = std::min(
                settings_.density.seaLevel - 8,
                settings_.density.waterTableBase +
                    int(std::floor((2 * aquifer - 1) * settings_.density.waterTableAmplitude)));
            int fillTop = optimized
                              ? std::max(top, settings_.density.water
                                                  ? std::max(settings_.density.seaLevel, table)
                                                  : top)
                              : 247;
            for (int y = 0; y <= fillTop; ++y) {
                auto state = s.blocks[idx(x + 1, y, z + 1)];
                if (state == BlockType::Stone && y > 3) {
                    if (y >= top - 3 && slope <= 1) {
                        state = settings_.density.water &&
                                        std::abs(top - settings_.density.seaLevel) <= 3
                                    ? BlockType::Sand
                                : y == top ? BlockType::Grass
                                           : BlockType::Dirt;
                    }
                } else if (state == BlockType::Air && settings_.density.water &&
                           ((y > top && y <= settings_.density.seaLevel) ||
                            (y <= table && aquifer > settings_.density.aquiferThreshold)))
                    state = BlockState::water();
                data->blocks[data->index(x, y, z)] = state;
            }
        }
    }
    for (int z = 0; z < 18; ++z)
        for (int x = 0; x < 18; ++x)
            data->heightHalo[z * 18 + x] = std::uint16_t(s.tops[z * 18 + x]);
    stampOres(c, *data);
    FeatureBounds bounds{bx, bx + 15, bz, bz + 15, 4, 247};
    auto villages = villagesIn(bounds);
    auto trees = treesIn(bounds);
    data->treeIncidences = std::uint32_t(trees.size());
    data->villageIncidences = std::uint32_t(villages.size());
    for (auto& v : villages)
        for (int z = 0; z < 16; ++z) {
            if (cancelled())
                return {};
            for (int x = 0; x < 16; ++x)
                for (int y = std::max(4, v.bounds.minY); y <= std::min(247, v.bounds.maxY); ++y)
                    data->blocks[data->index(x, y, z)] =
                        stampVillage(v, bx + x, y, bz + z, data->getState(x, y, z));
        }
    for (auto& t : trees)
        for (int z = 0; z < 16; ++z) {
            if (cancelled())
                return {};
            for (int x = 0; x < 16; ++x)
                for (int y = t.bounds.minY; y <= std::min(247, t.bounds.maxY); ++y)
                    data->blocks[data->index(x, y, z)] =
                        stampTree(t, bx + x, y, bz + z, data->getState(x, y, z));
        }
    if (settings_.density.flowers)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int y = s.tops[(z + 1) * 18 + x + 1] + 1;
                auto h = SeedDerivation::coordinateHash(
                    SeedDerivation::derive(settings_.seed, SeedDomain::Flowers), bx + x, 0, bz + z);
                if (y < 248 && data->get(x, y, z) == BlockType::Air &&
                    data->get(x, y - 1, z) == BlockType::Grass && h % 97 == 0)
                    data->blocks[data->index(x, y, z)] =
                        (h & 256) ? BlockType::RedFlower : BlockType::YellowFlower;
            }
    data->version = revision;
    data->rebuildSummaries();
    return data;
}
} // namespace bloxide
