#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/Coordinates.hpp>
#include <bloxide/DensityWorld.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bloxide {
ChunkGenerator::ChunkGenerator(const WorldSettings& s, NoiseBackend backend)
    : settings_(s), continental_(SeedDerivation::derive(s.seed, SeedDomain::Continental)),
      ridges_(SeedDerivation::derive(s.seed, SeedDomain::Ridges)),
      hills_(SeedDerivation::derive(s.seed, SeedDomain::Hills)),
      turbulence_(SeedDerivation::derive(s.seed, SeedDomain::Turbulence)),
      warpX_(SeedDerivation::derive(s.seed, SeedDomain::WarpX)),
      warpZ_(SeedDerivation::derive(s.seed, SeedDomain::WarpZ)),
      roughness_(SeedDerivation::derive(s.seed, SeedDomain::Roughness)),
      mountainMask_(SeedDerivation::derive(s.seed, SeedDomain::MountainMask)),
      valley_(SeedDerivation::derive(s.seed, SeedDomain::Valley)),
      plateau_(SeedDerivation::derive(s.seed, SeedDomain::Plateau)),
      caves_(SeedDerivation::derive(s.seed, SeedDomain::Caves)),
      caveWarpX_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpX)),
      caveWarpY_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpY)),
      caveWarpZ_(SeedDerivation::derive(s.seed, SeedDomain::CaveWarpZ)) {
    settings_.validate();
    if (s.generatorVersion == "density-world-1")
        density_ = std::make_shared<DensityWorld>(s, backend);
}
int ChunkGenerator::surfaceHeight(std::int64_t x, std::int64_t z) {
    static const ChunkGenerator generator;
    return generator.heightAt(x, z);
}
int ChunkGenerator::heightAt(std::int64_t x, std::int64_t z) const {
    return int(std::floor(sampleTerrain(x, z).height));
}
TerrainSample ChunkGenerator::sampleTerrain(std::int64_t x, std::int64_t z,
                                            double footprint) const {
    if (x < -WorldLimit - 256 || x > WorldLimit + 256 || z < -WorldLimit - 256 ||
        z > WorldLimit + 256)
        throw std::out_of_range("terrain coordinate out of bounds");
    if (!std::isfinite(footprint) || footprint <= 0)
        throw std::invalid_argument("invalid terrain footprint");
    if (settings_.preset == "flat")
        return {};
    auto warped = FractalNoise::warp2D(warpX_, warpZ_, settings_.warp2D, double(x), double(z),
                                       settings_.warpStrength, footprint);
    double wx = warped[0], wz = warped[1];
    auto continental = FractalNoise::sample2D(continental_, settings_.continental, wx, wz,
                                              FractalMode::Fbm, footprint);
    auto ridge =
        FractalNoise::sample2D(ridges_, settings_.ridges, wx, wz, FractalMode::Ridged, footprint);
    auto hills =
        FractalNoise::sample2D(hills_, settings_.hills, wx, wz, FractalMode::Billow, footprint);
    TerrainSample result;
    result.continentalness = (continental + 1) * 0.5;
    result.ridge = (ridge + 1) * 0.5;
    if (settings_.generatorVersion == "simplex-1") {
        auto erosion = FractalNoise::sample2D(turbulence_, settings_.turbulence, wx, wz,
                                              FractalMode::Turbulence, footprint);
        result.height =
            std::clamp(64 + 72 * continental + 58 * ridge + 12 * hills - 10 * erosion, 24.0, 192.0);
        return result;
    }
    auto unit = [&](const SimplexNoise2D& n, const FractalSettings& f) {
        return (FractalNoise::sample2D(n, f, wx, wz, FractalMode::Fbm, footprint) + 1) * 0.5;
    };
    auto smooth = [](double a, double b, double t) {
        t = std::clamp((t - a) / (b - a), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    result.roughness = unit(roughness_, settings_.roughness);
    result.mountainMask =
        smooth(settings_.maskLow, settings_.maskHigh, unit(mountainMask_, settings_.mountainMask));
    double v =
        FractalNoise::sample2D(valley_, settings_.valley, wx, wz, FractalMode::Fbm, footprint);
    result.valley = 1 - smooth(0.05, 0.35, std::abs(v));
    result.plateau = smooth(0.55, 0.8, unit(plateau_, settings_.plateau)) * (1 - result.valley);
    double base = 64 + 18 * continental;
    if (settings_.preset == "mountainous") {
        // Monotone cubic Hermite: harmonic interior slopes, endpoint secants.
        std::array<double, 4> d{};
        std::array<double, 5> m{};
        for (int i = 0; i < 4; ++i)
            d[i] = (settings_.splineY[i + 1] - settings_.splineY[i]) /
                   (settings_.splineX[i + 1] - settings_.splineX[i]);
        m[0] = d[0];
        m[4] = d[3];
        for (int i = 1; i < 4; ++i) {
            double a = settings_.splineX[i] - settings_.splineX[i - 1],
                   b = settings_.splineX[i + 1] - settings_.splineX[i];
            m[i] = (d[i - 1] == 0 || d[i] == 0)
                       ? 0
                       : (3 * (a + b)) / ((2 * b + a) / d[i - 1] + (b + 2 * a) / d[i]);
        }
        int i = 0;
        while (i < 3 && continental > settings_.splineX[i + 1])
            ++i;
        double width = settings_.splineX[i + 1] - settings_.splineX[i],
               t = std::clamp((continental - settings_.splineX[i]) / width, 0.0, 1.0), t2 = t * t,
               t3 = t2 * t;
        base = (2 * t3 - 3 * t2 + 1) * settings_.splineY[i] + (t3 - 2 * t2 + t) * width * m[i] +
               (-2 * t3 + 3 * t2) * settings_.splineY[i + 1] + (t3 - t2) * width * m[i + 1];
    }
    double detail = 0.25 + 0.75 * result.roughness;
    double height = base + settings_.hillsAmplitude * hills * detail +
                    settings_.ridgeAmplitude * result.mountainMask * result.ridge * detail -
                    settings_.valleyAmplitude * result.valley;
    double blend = settings_.plateauBlend * result.plateau;
    height = height * (1 - blend) + settings_.plateauHeight * blend;
    result.height = std::clamp(height, settings_.minHeight, settings_.maxHeight);
    return result;
}
double ChunkGenerator::caveNode(std::int64_t x, int y, std::int64_t z) const {
    auto warped = FractalNoise::warp3D(caveWarpX_, caveWarpY_, caveWarpZ_, settings_.caveWarp,
                                       double(x), y, double(z), settings_.caveWarpStrength, 4);
    return FractalNoise::sample3D(caves_, settings_.caveDensity, warped[0], warped[1], warped[2],
                                  FractalMode::Fbm, 4);
}
std::shared_ptr<ChunkData> ChunkGenerator::generate(ChunkCoord c, std::uint64_t revision,
                                                    const std::atomic<bool>* cancel,
                                                    bool optimized) const {
    if (c.x < -WorldLimit / 16 - 16 || c.x > WorldLimit / 16 + 15 || c.z < -WorldLimit / 16 - 16 ||
        c.z > WorldLimit / 16 + 15)
        throw std::out_of_range("chunk generation bounds");
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (cancelled())
        return {};
    if (density_)
        return density_->generate(c, revision, cancel, optimized);
    std::array<int, 256> heights{};
    int highest = 0;
    const auto baseX = c.x * 16, baseZ = c.z * 16;
    auto data = std::make_shared<ChunkData>();
    for (int z = -1; z <= 16; ++z) {
        if (cancelled())
            return {};
        for (int x = -1; x <= 16; ++x) {
            int h = heightAt(baseX + x, baseZ + z);
            data->heightHalo[(z + 1) * 18 + x + 1] = std::uint16_t(h);
            if (x >= 0 && x < 16 && z >= 0 && z < 16) {
                heights[z * 16 + x] = h;
                highest = std::max(highest, h);
            }
        }
    }
    const int levels = highest / 4 + 2;
    std::vector<double> density(std::size_t(5 * 5 * levels));
    auto index = [](int x, int y, int z) { return (y * 5 + z) * 5 + x; };
    if (settings_.caves)
        for (int y = 1; y < levels; ++y) {
            if (cancelled())
                return {};
            for (int z = 0; z < 5; ++z)
                for (int x = 0; x < 5; ++x)
                    density[index(x, y, z)] = caveNode(baseX + x * 4, y * 4, baseZ + z * 4);
        }
    data->blocks.fill(BlockType::Air);
    std::vector<unsigned char> classification(std::size_t(4 * 4 * (levels - 1)), 0);
    if (optimized && settings_.caves)
        for (int gy = 1; gy < levels - 1; ++gy)
            for (int gz = 0; gz < 4; ++gz)
                for (int gx = 0; gx < 4; ++gx) {
                    double lo = 1, hi = -1;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dz = 0; dz < 2; ++dz)
                            for (int dx = 0; dx < 2; ++dx) {
                                double v = density[index(gx + dx, gy + dy, gz + dz)];
                                lo = std::min(lo, v);
                                hi = std::max(hi, v);
                            }
                    classification[(gy * 4 + gz) * 4 + gx] = hi <= settings_.caveThreshold  ? 1
                                                             : lo > settings_.caveThreshold ? 2
                                                                                            : 0;
                }
    // Contiguous X row writes; untouched Air sections above the maximum surface.
    for (int section = 0; section <= highest / 16; ++section) {
        if (cancelled())
            return {};
        if (optimized && section > 0) {
            int low = section * 16, high = low + 15;
            bool allStone = true;
            for (auto h : heights)
                allStone &= high <= h - 4;
            double densityLow = 1, densityHigh = -1;
            if (settings_.caves)
                for (int gy = low / 4; gy <= std::min(high / 4 + 1, levels - 1); ++gy)
                    for (int gz = 0; gz < 5; ++gz)
                        for (int gx = 0; gx < 5; ++gx) {
                            double v = density[index(gx, gy, gz)];
                            densityLow = std::min(densityLow, v);
                            densityHigh = std::max(densityHigh, v);
                        }
            if (allStone && (!settings_.caves || densityHigh <= settings_.caveThreshold)) {
                std::fill_n(data->blocks.begin() + data->index(0, low, 0), 4096, BlockType::Stone);
                continue;
            }
            if (settings_.caves && densityLow > settings_.caveThreshold)
                continue;
        }
        for (int y = section * 16; y <= std::min(highest, section * 16 + 15); ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    int h = heights[z * 16 + x];
                    if (y > h)
                        continue;
                    if (y == 0) {
                        data->blocks[data->index(x, 0, z)] = BlockType::Bedrock;
                        continue;
                    }
                    bool carve = false;
                    if (settings_.caves && y >= 4) {
                        int gx = x / 4, gy = y / 4, gz = z / 4;
                        double fx = (x % 4) / 4.0, fy = (y % 4) / 4.0, fz = (z % 4) / 4.0;
                        auto kind = classification[(gy * 4 + gz) * 4 + gx];
                        if (kind == 2)
                            carve = true;
                        else if (kind == 0) {
                            double v = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dz = 0; dz < 2; ++dz)
                                    for (int dx = 0; dx < 2; ++dx)
                                        v += density[index(gx + dx, gy + dy, gz + dz)] *
                                             (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) *
                                             (dz ? fz : 1 - fz);
                            carve = v > settings_.caveThreshold;
                        }
                    }
                    if (!carve)
                        data->blocks[data->index(x, y, z)] = y == h       ? BlockType::Grass
                                                             : y >= h - 3 ? BlockType::Dirt
                                                                          : BlockType::Stone;
                }
    }
    data->rebuildSummaries();
    data->version = revision;
    return data;
}
} // namespace bloxide
