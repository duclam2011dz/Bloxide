#include <bloxide/DensityWorld.hpp>
#include <bloxide/Fluid.hpp>
#include <bloxide/Physics.hpp>
#include <bloxide/SurfaceHierarchy.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <bloxide/Textures.hpp>
#include <bloxide/Resources.hpp>
#include <bloxide/World.hpp>
#include <bloxide/Trace.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <unordered_map>
#include <cstring>
using namespace bloxide;
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (...) {
        threw = true;
    }
    require(threw, "expected validation error");
}
void noise() {
    SimplexNoise2D n2(130);
    SimplexNoise3D n3(130);
    double grid[17 * 11 * 7];
    NoiseGrid2D g2{-1.2, 0.4, 0.13, -0.17, 17, 11};
    n2.sampleGrid2D(g2, grid, std::size(grid));
    for (unsigned z = 0; z < 11; ++z)
        for (unsigned x = 0; x < 17; ++x)
            require(grid[z * 17 + x] == n2.sample(g2.x + x * g2.dx, g2.z + z * g2.dz),
                    "batch2D equivalence");
    NoiseGrid3D g3{-2.5, 1.2, 4.1, 0.11, -0.1, 0.17, 17, 7, 11};
    n3.sampleGrid3D(g3, grid, std::size(grid));
    for (unsigned y = 0; y < 7; ++y)
        for (unsigned z = 0; z < 11; ++z)
            for (unsigned x = 0; x < 17; ++x)
                require(grid[(y * 11 + z) * 17 + x] ==
                            n3.sample(g3.x + x * g3.dx, g3.y + y * g3.dy, g3.z + z * g3.dz),
                        "batch3D equivalence");
    double candidate[17 * 11 * 7];
    for (auto backend : {NoiseBackend::SSE2, NoiseBackend::AVX2})
        if (noiseBackendSupported(backend)) {
            for (auto seed : {UINT64_C(0), UINT64_C(130), UINT64_MAX}) {
                SimplexNoise2D n(seed);
                n.sampleGrid2D(g2, grid, std::size(grid));
                n.sampleGrid2D(g2, candidate, std::size(candidate), backend);
                require(std::memcmp(grid, candidate, g2.width * g2.depth * sizeof(double)) == 0,
                        "SIMD 2D bitwise mismatch");
                SimplexNoise3D m(seed);
                m.sampleGrid3D(g3, grid, std::size(grid));
                m.sampleGrid3D(g3, candidate, std::size(candidate), backend);
                require(std::memcmp(grid, candidate, sizeof(grid)) == 0,
                        "SIMD 3D bitwise mismatch");
            }
        }
    rejects([&] { n2.sampleGrid2D(g2, grid, 1); });
    rejects([&] {
        auto bad = g2;
        bad.x = INFINITY;
        n2.sampleGrid2D(bad, grid, std::size(grid));
    });
    for (double gain : {0.0, 0.5, 1.0})
        for (auto mode : {FractalMode::Fbm, FractalMode::Billow, FractalMode::Turbulence,
                          FractalMode::Ridged}) {
            FractalSettings s{6, 1.0 / 64, 2, gain};
            FractalKernel kernel(s);
            for (double f : {1, 4, 16})
                for (int i = -40; i < 40; ++i) {
                    require(kernel.sample2D(n2, i * 1.7, -i * 2.1, mode, f) ==
                                FractalNoise::sample2D(n2, s, i * 1.7, -i * 2.1, mode, f),
                            "cached2D exact");
                    require(kernel.sample3D(n3, i * 1.7, 17.3, -i * 2.1, mode, f) ==
                                FractalNoise::sample3D(n3, s, i * 1.7, 17.3, -i * 2.1, mode, f),
                            "cached3D exact");
                }
        }
}
void density() {
    auto settings = WorldSettings::densityWorld();
    settings.density.trees = false;
    settings.density.villages = false;
    settings.density.flowers = false;
    ChunkGenerator gen(settings);
    auto context = gen.densityContext();
    require(context, "density context");
    auto data = gen.generate({-1, 0});
    unsigned ores = 0, water = 0;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            require(data->get(x, 0, z) == BlockType::Bedrock, "protected bedrock");
            for (int y = 0; y < 248; y += 7)
                require(data->getState(x, y, z) == context->baseState(-16 + x, y, z),
                        "shared block sampler disagrees with generation");
        }
    for (auto c : std::array<ChunkCoord, 4>{
             {{0, 0}, {-1, -1}, {4, 3}, {WorldLimit / 16 - 8, -WorldLimit / 16 + 8}}}) {
        auto a = gen.generate(c),
             b = std::async(std::launch::async, [&] { return gen.generate(c); }).get();
        require(a->blocks == b->blocks, "threaded density reproducibility");
        for (auto backend : {NoiseBackend::SSE2, NoiseBackend::AVX2})
            if (noiseBackendSupported(backend)) {
                auto simd = ChunkGenerator(settings, backend).generate(c);
                require(simd->blocks == a->blocks, "SIMD whole chunk mismatch");
            }
        require(gen.generate(c, 1, nullptr, false)->blocks == a->blocks,
                "cached-grid reference mismatch");
        for (auto s : a->blocks) {
            ores +=
                s == BlockType::IronOre || s == BlockType::GoldOre || s == BlockType::DiamondOre;
            water += s == BlockType::Water;
        }
    }
    require(ores > 0 && water > 0, "ore/water fields missing");
    auto filtered = settings;
    filtered.density.terrain = {8, 1.0 / 24, 2, 0.5};
    filtered.density.warp = {7, 1.0 / 32, 2, 0.5};
    ChunkGenerator filteredGen(filtered);
    require(filteredGen.generate({-1, 0})->blocks ==
                filteredGen.generate({-1, 0}, 1, nullptr, false)->blocks,
            "configured Nyquist filtering changed reference output");
    rejects([&] { context->generate({INT64_MAX, 0}, 1, nullptr); });
    for (int y = 4; y < 180; y += 11) {
        require(std::isfinite(context->density(-1, y, 0)), "density finite");
    }
    auto flat = WorldSettings::densityWorld("flat");
    flat.caves = false;
    flat.density.water = false;
    flat.density.ores = false;
    flat.density.flowers = false;
    DensityWorld features(flat);
    auto villages = features.villagesIn({-1024, 1024, -1024, 1024, 0, 255});
    require(!villages.empty(), "flat safe village fixture");
    for (auto& v : villages) {
        require(v.houses.size() >= 3 && v.houses.size() <= 6, "house count");
        auto& house = v.houses.front();
        require(features.sampleState(house.x, house.y, house.z) == BlockType::OakPlanks,
                "house floor stamp");
        require(features.sampleState(house.x + house.width / 2, house.y + 1, house.z) ==
                    BlockType::Air,
                "door opening");
    }
    auto trees = features.treesIn({-128, 128, -128, 128, 0, 255});
    require(!trees.empty(), "tree fixture");
    for (auto& t : trees)
        require(features.sampleState(t.x, t.y, t.z) == BlockType::OakLog ||
                    features.sampleState(t.x, t.y, t.z) == BlockType::BirchLog ||
                    features.sampleState(t.x, t.y, t.z) == BlockType::PineLog,
                "trunk stamp");
    auto dir =
        std::filesystem::path("testing") /
        ("v140-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    settings.save(dir / "world.meta");
    auto round = WorldSettings::load(dir / "world.meta");
    require(round.generatorVersion == "density-world-1" &&
                round.density.ore[2].probability == settings.density.ore[2].probability,
            "schema3");
    require(WorldSettings::open(dir).generatorVersion == "density-world-1", "reopen");
    std::filesystem::remove_all(dir);
}
using Grid = std::unordered_map<Int3, BlockState, VoxelCoordHash>;
Grid runWater(int fps) {
    Grid world;
    world[{0, 5, 0}] = BlockState::water();
    FluidEngine fluid;
    fluid.notify({0, 5, 0});
    auto query = [&](const Int3& p) {
        if (p.y == 0)
            return FluidCell{BlockType::Stone, true};
        return FluidCell{world[p], true};
    };
    auto apply = [&](const std::vector<FluidUpdate>& changes) {
        for (auto& u : changes)
            world[u.position] = u.after;
    };
    for (int i = 0; i < fps * 3; ++i)
        fluid.advance(1.0 / fps, query, apply);
    require(world[{0, 1, 0}] == BlockType::Water, "waterfall did not reach floor");
    require(world[{1, 1, 0}] == BlockType::Water, "water did not spread");
    require(world[{8, 1, 0}] == BlockType::Air, "flow exceeded level seven");
    world[{0, 5, 0}] = BlockType::Air;
    fluid.notify({0, 5, 0});
    for (int i = 0; i < fps * 4; ++i)
        fluid.advance(1.0 / fps, query, apply);
    for (auto& [p, s] : world)
        require(s != BlockType::Water, "flow did not drain");
    require(fluid.stats().ticks == 70, "tick count depends on frame rate");
    return world;
}
void fluids() {
    runWater(30);
    runWater(60);
    runWater(144);
    Grid world;
    world[{-1, 1, 0}] = BlockState::water();
    world[{1, 1, 0}] = BlockState::water();
    FluidEngine fluid;
    fluid.notify({0, 1, 0});
    auto changes = fluid.tick([&](const Int3& p) {
        return FluidCell{p.y == 0 ? BlockState(BlockType::Stone) : world[p], true};
    });
    for (auto& u : changes)
        world[u.position] = u.after;
    require(world[{0, 1, 0}].source(), "two-source conversion");
    fluid.setRegion({64, 64});
    require(fluid.stats().frozen > 0, "fluid frontier not frozen");
    fluid.setRegion({0, 0});
    require(fluid.stats().frontier > 0, "frontier did not resume");
    PlayerPhysics p;
    p.feet = {0.5, 1, 0.5};
    auto solid = [](std::int64_t, int y, std::int64_t) { return y <= 0; };
    auto water = [](std::int64_t, int y, std::int64_t) {
        return y > 0 && y <= 5 ? BlockState::water() : BlockState{};
    };
    require(p.submergedFraction(water) > 0.99, "submerged AABB volume");
    p.step(0.1f, 1, 0, true, solid, {}, water);
    require(p.velocity[1] > 0 && p.velocity[0] > 0, "swimming controls");
}
void hierarchy() {
    auto blocks = std::make_shared<ChunkData>();
    for (int z = 3; z < 13; ++z)
        for (int x = 3; x < 13; ++x)
            for (int y = 8; y < 22; ++y) {
                double dx = x - 7.5, dz = z - 7.5, dy = y - 14.5;
                if (dx * dx + dz * dz + dy * dy < 40)
                    blocks->set(x, y, z, BlockType::Stone);
            }
    auto fine = ChunkMesher{}.build({}, blocks, {}, 0);
    auto coarse = SurfaceHierarchy::build(*fine, 8);
    require(coarse->surface3D && !coarse->surfaceVertices.empty(), "3D surface format");
    require(SurfaceHierarchy::validMorph(*coarse), "unsafe XYZ morph");
    unsigned moved = 0;
    for (auto& v : coarse->surfaceVertices) {
        moved += v.x != v.fromX || v.y != v.fromY || v.z != v.fromZ;
        if (v.fromX <= 1 || v.fromX >= 15 || v.fromZ <= 1 || v.fromZ >= 15)
            require(v.x == v.fromX && v.y == v.fromY && v.z == v.fromZ, "boundary moved");
    }
    std::cout << "hierarchy accepted=" << coarse->collapseAccepted
              << " rejected=" << coarse->collapseRejected << " moved=" << moved
              << " triangles=" << coarse->surfaceIndices.size() / 3 << " -> "
              << coarse->coarseIndices.size() / 3 << '\n';
    require(coarse->coarseIndices.size() <= coarse->surfaceIndices.size(), "LOD expanded indices");
    require(moved > 0 || coarse->collapseAccepted == 0, "fallback retained unsafe parent mappings");
    ChunkMesh plane;
    for (unsigned z = 3; z <= 13; ++z)
        for (unsigned x = 3; x <= 13; ++x)
            plane.vertices.push_back({x | (12U << 5) | (z << 14) | (12U << 19) | (2U << 26)});
    for (unsigned z = 0; z < 10; ++z)
        for (unsigned x = 0; x < 10; ++x) {
            unsigned a = z * 11 + x;
            plane.indices.insert(plane.indices.end(), {a, a + 11, a + 1, a + 1, a + 11, a + 12});
        }
    auto safePlane = SurfaceHierarchy::build(plane, 2);
    require(safePlane->collapseAccepted > 0 && SurfaceHierarchy::validMorph(*safePlane),
            "safe planar hierarchy did not coarsen");
    auto refined = SurfaceHierarchy::build(*fine, 2, coarse, 0.37f);
    require(SurfaceHierarchy::validMorph(*refined), "interrupted morph");
}
void materials() {
    require(TextureLayers == 112, "catalog count");
    for (unsigned group = 5; group < TextureGroups.size(); ++group) {
        auto image = TextureGenerator::generate(group, 0);
        auto policy = textureRenderClass(group);
        for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
            if (policy == RenderClass::Opaque)
                require(image.rgba[i] == 255, "opaque alpha");
            if (policy == RenderClass::Cutout)
                require(image.rgba[i] == 0 || image.rgba[i] == 255, "cutout alpha");
            if (policy == RenderClass::Translucent)
                require(image.rgba[i] < 255, "translucent alpha");
        }
        if (policy == RenderClass::Cutout) {
            auto levels = cutoutMipChain(image);
            require(levels.size() == 6, "cutout mip levels");
            std::size_t base = 0;
            for (std::size_t i = 3; i < 4096; i += 4)
                base += image.rgba[i] >= 128;
            for (auto& level : levels) {
                std::size_t count = 0;
                for (std::size_t i = 3; i < level.rgba.size(); i += 4)
                    count += level.rgba[i] >= 128;
                require(count ==
                            std::size_t(std::lround(base / 1024.0 * level.width * level.height)),
                        "cutout coverage changed");
            }
        }
    }
    auto data = std::make_shared<ChunkData>();
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            data->set(x, 10, z, BlockType::Stone);
            data->setState(x, 11, z, BlockState::water());
        }
    auto mesh = ChunkMesher{}.build({}, data, {}, 0);
    unsigned top = 0;
    for (auto& face : mesh->translucentFaces) {
        auto v = mesh->translucentVertices[mesh->translucentIndices[face.first]];
        if (v.face == 2)
            ++top;
    }
    require(top == 1, "water source top not greedy merged");
    data->set(2, 12, 2, BlockType::OakLeaves);
    data->set(4, 12, 4, BlockType::RedFlower);
    mesh = ChunkMesher{}.build({}, data, {}, 0);
    require(!mesh->cutoutVertices.empty() && !mesh->plantVertices.empty(), "cutout passes missing");
}
void resources() {
    ResourceLimits limits;
    limits.validate();
    limits.generationQueue = 129;
    rejects([&] { limits.validate(); });
    limits = ResourceLimits{};
    QueueCredits queue(3);
    std::atomic<unsigned> acquired{0};
    std::vector<std::future<void>> workers;
    for (unsigned i = 0; i < 16; ++i)
        workers.push_back(std::async(std::launch::async, [&] {
            if (queue.acquire())
                ++acquired;
        }));
    for (auto& job : workers)
        job.get();
    require(acquired == 3 && queue.used == 3 && !queue.acquire(), "queue exceeded capacity");
    for (unsigned i = 0; i < 3; ++i)
        queue.release();
    require(queue.used == 0, "queue credit leak");
    DistanceBudget distance;
    distance.update(0.1, limits.cpuBytes, 0, limits);
    require(distance.effective == 23, "budget did not reduce far radius");
    distance.update(1.9, 0, 0, limits);
    require(distance.effective == 23, "distance recovered without hysteresis");
    distance.update(0.11, 0, 0, limits);
    require(distance.effective == 24, "distance did not recover");
    distance.effective = 23;
    distance.update(1.9, 0, 0, limits);
    distance.update(1, 0, 0, limits, false);
    distance.update(0.2, 0, 0, limits);
    require(distance.effective == 23, "insufficient growth reserve did not reset recovery");
    distance.update(2, 0, 0, limits);
    require(distance.effective == 24, "growth headroom did not restore recovery");
    for (unsigned i = 0; i < 40; ++i)
        distance.update(0.1, limits.cpuBytes, limits.gpuBytes, limits);
    require(distance.effective == 8, "budget removed required near radius");
    auto& trace = TraceBuffer::instance();
    trace.clear();
    trace.setEnabled(true);
    for (unsigned i = 0; i < 65540; ++i)
        trace.push({});
    require(trace.snapshot().size() == 65536 && trace.overwritten() == 4,
            "unbounded trace capture");
    trace.setEnabled(false);
    trace.clear();
    auto settings = WorldSettings::densityWorld("flat");
    settings.density.water = false;
    settings.density.trees = false;
    settings.density.villages = false;
    World world(settings);
    limits.generationQueue = 3;
    limits.meshingQueue = 2;
    limits.uploadQueue = 1;
    world.setResourceLimits(limits);
    world.failNextMeshForTests();
    auto start = std::chrono::steady_clock::now();
    while (world.stats().ready < 9) {
        world.update({8, 80, 8}, {0, 0, -1});
        auto stats = world.stats();
        require(stats.generationQueue <= 3 && stats.meshingQueue <= 2 && stats.uploadStaging <= 1,
                "pipeline backpressure exceeded cap");
        for (auto& upload : world.takePendingUploads())
            world.markUploaded(upload);
        for (auto& group : world.readyGroups())
            world.commitGroup(group);
        require(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
                    30,
                "pipeline staging/retry deadlock");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    World handoff(settings, true);
    auto near = WorldPosition::fromAbsolute(8, 80, 8);
    auto pump = [&](const WorldPosition& p) {
        handoff.updateAt(p, {0, 0, -1});
        for (auto& u : handoff.takePendingUploads())
            handoff.markUploaded(u);
        for (auto& g : handoff.readyGroups())
            require(handoff.commitGroup(g), "handoff publication failed");
    };
    auto settle = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        do {
            pump(near);
            if (handoff.stats().ready == 289 && handoff.heightmapSettled())
                return;
            require(std::chrono::steady_clock::now() < deadline,
                    "cancelled near/far remesh did not recover");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
    };
    limits = ResourceLimits{};
    limits.requestedFarRadius = 9;
    handoff.setResourceLimits(limits);
    settle();
    handoff.setMeshDelayForTests(20);
    require(handoff.set(15, 64, 8, BlockType::Air), "boundary handoff edit");
    pump(near);
    pump(WorldPosition::fromAbsolute(152, 80, 8));
    pump(near);
    handoff.setMeshDelayForTests(0);
    settle();
    require(handoff.targetable(15, 8), "near target remained locked after re-entry");
}
int main() {
    try {
        noise();
        density();
        fluids();
        hierarchy();
        materials();
        resources();
        std::cout << "v1.4 core "
                     "noise/density/features/metadata/fluid/swimming/hierarchy/materials/resources "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
