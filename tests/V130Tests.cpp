#include <bloxide/World.hpp>
#include <bloxide/Physics.hpp>
#include <bloxide/Textures.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace bloxide;
std::atomic<bool> countAllocations{false};
std::atomic<std::size_t> allocations{0};
void* volatile allocationProbe = nullptr;
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
// TSan owns strong global new/delete interceptors. Keep the allocation-free
// assertion using its public allocator hook instead of replacing interceptors.
extern "C" void __sanitizer_malloc_hook(const volatile void*, std::size_t) {
    if (countAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
}
#else
void* operator new(std::size_t n) {
    if (countAllocations)
        ++allocations;
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
#endif
void require(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid input was accepted");
}
void collision() {
    for (std::int64_t base :
         std::array<std::int64_t, 6>{0, 15, -1, -16, WorldLimit - 64, -WorldLimit + 64})
        for (int axis : {0, 2})
            for (int sign : {-1, 1})
                for (double eps : {0.0, 1e-7, -1e-7}) {
                    auto start = WorldPosition::fromAbsolute(double(base) + 0.5, 1 + eps,
                                                             double(base) + 0.5);
                    if (axis == 0)
                        start = WorldPosition::fromAbsolute(double(base) + 0.5 - sign * 2.0,
                                                            1 + eps, double(base) + 0.5);
                    else
                        start = WorldPosition::fromAbsolute(double(base) + 0.5, 1 + eps,
                                                            double(base) + 0.5 - sign * 2.0);
                    PlayerPhysics p;
                    p.origin = start.chunk;
                    p.feet = start.local();
                    p.grounded = true;
                    auto solid = [&](std::int64_t x, int y, std::int64_t z) {
                        return y <= 0 || (x == base && z == base && y == 1);
                    };
                    for (int frame = 0; frame < 120; ++frame) {
                        p.step(1.0f / 120, axis == 0 ? float(sign) : 0, axis == 2 ? float(sign) : 0,
                               false, solid);
                        require(std::abs(p.feet[1] - 1) < 1e-6, "side contact lifted player");
                    }
                    double relative =
                        p.feet[axis] - double(base - (axis == 0 ? p.origin.x : p.origin.z) * 16);
                    double expected = sign > 0 ? -double(PlayerPhysics::Radius)
                                               : 1 + double(PlayerPhysics::Radius);
                    require(std::abs(relative - expected) < 1e-8,
                            "one horizontal face failed to block");
                    require(p.clear(solid), "collision left overlap");
                }
    // This 2e-5 side penetration made the old destination-box Y solver choose
    // blockTop-feetY = +1 while moving downward.
    PlayerPhysics p;
    p.feet = {-double(PlayerPhysics::Radius) + 2e-5, 1, 0.5};
    p.grounded = true;
    auto solid = [](std::int64_t x, int y, std::int64_t z) {
        return y <= 0 || (x == 0 && y == 1 && z == 0);
    };
    p.step(0.01f, 0, 0, false, solid);
    require(p.clear(solid) && std::abs(p.feet[1] - 1) < 1e-9, "overlap recovery teleported upward");
    p.origin = {};
    p.feet = {0.5, 12, 0.5};
    p.velocity = {0, -40, 0};
    p.grounded = false;
    p.step(0.5f, 0, 0, false, [](std::int64_t, int y, std::int64_t) { return y <= 0; });
    require(std::abs(p.feet[1] - 1) < 1e-9 && p.grounded, "swept floor tunneling");
    p.origin = {};
    p.feet = {0.5, 1, 0.5};
    p.velocity = {0, 40, 0};
    p.grounded = false;
    p.step(0.1f, 0, 0, false, [](std::int64_t, int y, std::int64_t) { return y <= 0 || y == 3; });
    require(p.feet[1] + PlayerPhysics::Height <= 3 + 1e-8, "ceiling face failed");
    auto position = WorldPosition::fromAbsolute(-0.25, 80, 16.125);
    require(position.chunk == ChunkCoord{-1, 1} && position.x == 15.75 && position.z == 0.125,
            "negative chunk normalization");
    auto far =
        WorldPosition::fromAbsolute(double(WorldLimit) - 16.125, 80, -double(WorldLimit) + 16.125);
    p.origin = far.chunk;
    p.feet = far.local();
    p.velocity = {5, 0, 0};
    auto before = p.worldEye();
    p.feet[0] += 0.5;
    p.rebase();
    auto after = p.worldEye();
    require(std::abs(double(after.blockX() - before.blockX()) + (after.x - std::floor(after.x)) -
                     (before.x - std::floor(before.x)) - 0.5) < 1e-9,
            "rebase changed world position");
    require(p.velocity[0] == 5, "rebase changed velocity");
    auto ray = traceWorldVoxels(after, {1, 0, 0}, 6, [&](std::int64_t x, int, std::int64_t z) {
        return TargetVoxel{x == after.blockX() + 2 && z == after.blockZ() ? BlockType::Stone
                                                                          : BlockType::Air,
                           false, 1, 1};
    });
    require(ray.hit && ray.block.x == after.blockX() + 2, "int64 targeting at world limit");
    rejects([] { WorldPosition::fromAbsolute(double(WorldLimit) + 1, 80, 0); });
}
void noise() {
    SimplexNoise2D n2(42), again(42), other(43);
    SimplexNoise3D n3(42);
    require(std::abs(n2.sample(0.1, 0.2) - (-0.39316632676048474)) < 1e-13, "2D golden");
    require(std::abs(n2.sample(-0.25, 0.75) - (-0.23362654220549314)) < 1e-13,
            "negative 2D golden");
    require(std::abs(n2.sample(100.25, -77.5) - (-0.74610811912814623)) < 1e-13, "large 2D golden");
    require(std::abs(n3.sample(0.1, 0.2, 0.3) - 0.17228267107773784) < 1e-13, "3D golden");
    require(std::abs(n3.sample(-0.25, 0.75, -1.25) - 0.92128357293259078) < 1e-13,
            "negative 3D golden");
    require(std::abs(n3.sample(100.25, -77.5, 33.25) - (-0.24807345772507508)) < 1e-13,
            "large 3D golden");
    require(n2.sample(1.1, 2.3) == again.sample(1.1, 2.3) &&
                n2.sample(1.1, 2.3) != other.sample(1.1, 2.3),
            "seeded noise determinism");
    double minimum = 1, maximum = -1;
    for (int x = -70; x <= 70; ++x)
        for (int z = -70; z <= 70; ++z) {
            double a = n2.sample(x * 0.13, z * 0.17), b = n3.sample(x * 0.13, 0.29, z * 0.17);
            require(std::isfinite(a) && std::abs(a) <= 1 && std::isfinite(b) && std::abs(b) <= 1,
                    "noise normalization bounds");
            minimum = std::min({minimum, a, b});
            maximum = std::max({maximum, a, b});
        }
    std::cout << "noise_min=" << minimum << " noise_max=" << maximum << std::endl;
    for (double point : {-16.0, -1.0, 0.0, 1.0, 16.0, 1000000.0}) {
        require(std::abs(n2.sample(point - 1e-7, 0.31) - n2.sample(point + 1e-7, 0.31)) < 1e-4,
                "2D continuity");
        require(std::abs(n3.sample(point - 1e-7, 0.31, -0.75) -
                         n3.sample(point + 1e-7, 0.31, -0.75)) < 1e-4,
                "3D continuity");
    }
    require(std::isfinite(n3.sample(17592186044416.0, -17592186044416.0, 0)),
            "large numeric safety");
    rejects([&] { n2.sample(std::numeric_limits<double>::infinity(), 0); });
    rejects([&] { n3.sample(0, std::numeric_limits<double>::quiet_NaN(), 0); });
    rejects([&] { n2.sample(1e30, 0); });
    FractalSettings f{5, 1.0 / 64, 2, 0.5};
    f.validate();
    for (auto mode :
         {FractalMode::Fbm, FractalMode::Billow, FractalMode::Ridged, FractalMode::Turbulence}) {
        double value = FractalNoise::sample2D(n2, f, 123, -71, mode);
        require(std::isfinite(value) && std::abs(value) <= 1, "fractal normalization");
        require(std::abs(FractalNoise::sample3D(n3, f, 123, 31, -71, mode)) <= 1,
                "3D fractal normalization");
    }
    auto four = f;
    four.octaves = 4;
    require(FractalNoise::sample2D(n2, f, 123, -71, FractalMode::Fbm, 4) ==
                FractalNoise::sample2D(n2, four, 123, -71),
            "octave filtering/normalization");
    require(FractalNoise::warp2D(n2, other, f, 12, 34, 0) == std::array<double, 2>{12, 34},
            "zero warp");
    auto warped = FractalNoise::warp3D(n3, n3, n3, f, 12, 34, 56, 12);
    require(warped != std::array<double, 3>{12, 34, 56}, "3D domain warp");
    f.gain = 2;
    rejects([&] { f.validate(); });
    f.gain = 0.5;
    auto single = f;
    single.gain = 0;
    single.validate();
    require(FractalNoise::sample2D(n2, single, 123, -71) ==
                n2.sample(123 * single.frequency, -71 * single.frequency),
            "zero gain uses first octave");
    allocations = 0;
    countAllocations = true;
    allocationProbe = ::operator new(32);
    ::operator delete(allocationProbe);
    countAllocations = false;
    require(allocations > 0, "allocation counter hook is inactive");
    allocations = 0;
    countAllocations = true;
    volatile double sink = 0;
    for (int i = 0; i < 1000; ++i) {
        sink += n2.sample(i * .13, -i * .17) + n3.sample(i * .13, 17, -i * .17);
        sink += FractalNoise::sample2D(n2, f, i, -i);
    }
    countAllocations = false;
    require(allocations == 0, "noise sample allocated memory");
    std::set<std::uint64_t> seeds;
    for (int tag = 1; tag <= 12; ++tag)
        seeds.insert(SeedDerivation::derive(0, static_cast<SeedDomain>(tag)));
    require(seeds.size() == 12, "feature seed isolation");
    auto hash = SeedDerivation::coordinateHash(0, std::numeric_limits<std::int64_t>::min(), -1,
                                               std::numeric_limits<std::int64_t>::max());
    require(hash == SeedDerivation::coordinateHash(0, std::numeric_limits<std::int64_t>::min(), -1,
                                                   std::numeric_limits<std::int64_t>::max()),
            "int64 coordinate hash");
}
std::uint64_t digest(const ChunkData& d) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto b : d.blocks) {
        h ^= static_cast<unsigned char>(b);
        h *= 1099511628211ULL;
    }
    return h;
}
void terrain() {
    ChunkGenerator generator;
    WorldSettings other;
    other.seed = 0;
    ChunkGenerator alternate(other);
    auto base = generator.generate({0, 0}), copy = generator.generate({0, 0});
    require(digest(*base) == 2061680697937486093ULL, "simplex-1 golden chunk digest");
    require(digest(*base) == digest(*copy) && digest(*base) != digest(*alternate.generate({0, 0})),
            "seed-based chunks");
    int caves = 0, mouths = 0;
    for (int cz = -2; cz <= 2; ++cz)
        for (int cx = -2; cx <= 2; ++cx) {
            auto data = generator.generate({cx, cz});
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    int h = generator.heightAt(cx * 16 + x, cz * 16 + z);
                    require(h >= 24 && h <= 192, "mountain height bounds");
                    require(data->get(x, 0, z) == BlockType::Bedrock &&
                                data->get(x, 3, z) == BlockType::Stone,
                            "protected cave floor");
                    require(data->get(x, h + 1, z) == BlockType::Air, "air above surface");
                    mouths += data->get(x, h, z) == BlockType::Air;
                    for (int y = 4; y < h - 3; ++y)
                        caves += data->get(x, y, z) == BlockType::Air;
                }
        }
    require(caves > 0 && mouths > 0, "caves and natural entrances exist");
    std::cout << "cave_air=" << caves << " mouths=" << mouths << std::endl;
    auto far = generator.generate({WorldLimit / 16 - 10, -WorldLimit / 16 + 10});
    require(far->get(0, 0, 0) == BlockType::Bedrock, "extreme chunk generation");
    auto mesh = ChunkMesher{}.build({}, base, {}, 1);
    require(mesh->lod == 0, "cave mouths require full meshes");
    // Shared grid nodes are identical from either side of a chunk boundary.
    auto density = [&](double x, double y, double z) {
        auto gx = static_cast<std::int64_t>(std::floor(x / 4)) * 4,
             gz = static_cast<std::int64_t>(std::floor(z / 4)) * 4;
        int gy = int(std::floor(y / 4)) * 4;
        double fx = (x - gx) / 4, fy = (y - gy) / 4, fz = (z - gz) / 4, result = 0;
        for (int dy = 0; dy < 2; ++dy)
            for (int dz = 0; dz < 2; ++dz)
                for (int dx = 0; dx < 2; ++dx)
                    result += generator.caveNode(gx + dx * 4, gy + dy * 4, gz + dz * 4) *
                              (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
        return result;
    };
    for (double boundary : {-16.0, 0.0, 16.0})
        for (int y = 4; y < 160; y += 7)
            require(std::abs(density(boundary - 1e-6, y, -17) - density(boundary + 1e-6, y, -17)) <
                        1e-5,
                    "cave interpolation seam");
}
void metadata() {
    auto dir =
        std::filesystem::path("testing") /
        ("v130-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    WorldSettings s;
    s.seed = std::numeric_limits<std::uint64_t>::max();
    s.continental.frequency = 0.001234567890123456;
    auto file = dir / "world.meta";
    s.save(file);
    auto loaded = WorldSettings::load(file);
    require(loaded.seed == s.seed && loaded.continental.frequency == s.continental.frequency &&
                loaded.generatorVersion == s.generatorVersion,
            "metadata round trip");
    require(WorldSettings::open(dir).seed == s.seed, "reopen seed");
    rejects([&] { WorldSettings::open(dir, 0); });
    {
        std::ofstream reserved(file.string() + ".tmp");
        reserved << "reserved";
    }
    rejects([&] { s.save(file); });
    require(WorldSettings::load(file).seed == s.seed,
            "atomic replacement preserves metadata on failure");
    std::filesystem::remove(file.string() + ".tmp");
    auto random = WorldSettings::open(dir / "random");
    require(WorldSettings::open(dir / "random").seed == random.seed, "random seed stored once");
    require(WorldSettings::parseSeed("18446744073709551615") == s.seed &&
                WorldSettings::parseSeed("0") == 0,
            "uint64 seed parsing");
    rejects([] { WorldSettings::parseSeed("-1"); });
    rejects([] { WorldSettings::parseSeed("18446744073709551616"); });
    std::ifstream input(file);
    std::stringstream buffer;
    buffer << input.rdbuf();
    auto text = buffer.str();
    input.close();
    text.replace(text.find("simplex-1"), 9, "simplex-2");
    {
        std::ofstream out(file);
        out << text;
    }
    rejects([&] { WorldSettings::load(file); });
    {
        std::ofstream out(file);
        out << "format_version 1\nseed NaN\n";
    }
    rejects([&] { WorldSettings::load(file); });
    std::filesystem::remove_all(dir);
}
int main() {
    try {
        collision();
        noise();
        terrain();
        metadata();
        std::cout << "v1.3 collision, noise, caves, floating origin and metadata passed"
                  << std::endl;
    } catch (const std::exception& e) {
        countAllocations = false;
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
