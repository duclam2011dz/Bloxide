#include <bloxide/World.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <future>
using namespace bloxide;
void check(bool v, const char* why) {
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
    check(threw, "expected rejection");
}
std::uint64_t digest(const ChunkData& d) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto b : d.blocks) {
        h ^= unsigned(b);
        h *= 1099511628211ULL;
    }
    return h;
}
void generation() {
    check(digest(*ChunkGenerator{}.generate({0, 0})) == 2061680697937486093ULL,
          "simplex-1 golden changed");
    check(digest(*ChunkGenerator(WorldSettings::terrainFields()).generate({0, 0})) ==
              15604160045169882075ULL,
          "terrain-fields-1 golden changed");
    for (auto name : {"flat", "rolling-hills", "mountainous"})
        for (bool caves : {false, true}) {
            auto s = WorldSettings::terrainFields(name);
            s.caves = caves;
            ChunkGenerator gen(s);
            for (auto coord : std::array<ChunkCoord, 4>{
                     {{0, 0}, {-1, 1}, {-7, -2}, {WorldLimit / 16 - 8, -WorldLimit / 16 + 8}}}) {
                auto fast = gen.generate(coord), reference = gen.generate(coord, 1, nullptr, false);
                check(fast->blocks == reference->blocks,
                      "section classification changed block output");
                check(fast->version == 1 && !fast->edited, "direct generation triggered edits");
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        auto sample = gen.sampleTerrain(coord.x * 16 + x, coord.z * 16 + z);
                        check(fast->heightHalo[(z + 1) * 18 + x + 1] ==
                                  int(std::floor(sample.height)),
                              "height cache mismatch");
                        check(fast->get(x, 0, z) == BlockType::Bedrock, "bedrock");
                        check(sample.height >= 4 && sample.height <= 224, "height bounds");
                        for (double footprint : {2, 4, 8})
                            check(std::isfinite(gen.sampleTerrain(coord.x * 16 + x,
                                                                  coord.z * 16 + z, footprint)
                                                    .height),
                                  "filtered height");
                        if (!caves)
                            check(fast->get(x, int(std::floor(sample.height)), z) ==
                                      BlockType::Grass,
                                  "surface mismatch");
                    }
                auto copy = *fast;
                copy.rebuildSummaries();
                check(copy.sectionSolid == fast->sectionSolid, "section summary");
            }
            auto a = gen.generate({-1, 0}), b = gen.generate({0, 0});
            for (int z = 0; z < 18; ++z)
                check(a->heightHalo[z * 18 + 17] == b->heightHalo[z * 18 + 1], "halo seam");
        }
    auto s = WorldSettings::terrainFields();
    ChunkGenerator gen(s);
    std::atomic<bool> cancel{true};
    check(!gen.generate({}, 1, &cancel), "cancelled generation published data");
    auto future = std::async(std::launch::async, [&] {
        cancel = false;
        return gen.generate({}, 1, &cancel);
    });
    cancel = true;
    auto result = future.get();
    check(!result || result->get(0, 0, 0) == BlockType::Bedrock, "partial generation result");
}
void mesh() {
    ChunkGenerator gen(WorldSettings::terrainFields());
    rejects([&] {
        HeightTileInput bad;
        bad.coord = {INT64_MAX, 0};
        HeightmapMesher::build(gen, bad);
    });
    check(HeightmapMesher::selectStep(13, 2) == 2 && HeightmapMesher::selectStep(14, 2) == 4,
          "LOD hysteresis2");
    check(HeightmapMesher::selectStep(18, 8) == 8 && HeightmapMesher::selectStep(17, 8) == 4,
          "LOD hysteresis8");
    for (int step : {2, 4, 8}) {
        HeightTileInput a;
        a.coord = {-1, 0};
        a.step = step;
        a.skirts.fill(false);
        auto left = HeightmapMesher::build(gen, a);
        a.coord = {0, 0};
        a.step = 2;
        auto right = HeightmapMesher::build(gen, a);
        check(left->vertices.empty() && left->heightmap, "LOD allocated voxel vertices");
        for (int z = 0; z <= 16; ++z) {
            auto l = HeightmapMesher::heightOnMesh(*left, 16, float(z)),
                 r = HeightmapMesher::heightOnMesh(*right, 0, float(z));
            check(std::abs(l - r) < 1e-5, "mixed LOD edge crack");
        }
        for (auto i : left->indices)
            check(i < left->heightVertices.size(), "invalid mesh index");
        a.coord = {-1, 0};
        a.step = step == 2 ? 8 : 2;
        a.previous = left;
        auto replacement = HeightmapMesher::build(gen, a);
        for (auto& v : replacement->heightVertices)
            if (v.x > 0 && v.x < 16 && v.z > 0 && v.z < 16)
                check(std::abs(v.fromY - HeightmapMesher::heightOnMesh(*left, v.x, v.z)) < 1e-4,
                      "morph start mismatch");
        for (auto& v : replacement->heightVertices)
            check(std::abs(HeightmapMesher::heightOnMesh(*replacement, v.x, v.z, 1) - v.y) < 1e-4,
                  "morph endpoint mismatch");
    }
    HeightTileInput in;
    in.step = 4;
    in.voxelEdge[0] = true;
    in.edges[0].fill(91);
    auto seam = HeightmapMesher::build(gen, in);
    check(std::abs(HeightmapMesher::heightOnMesh(*seam, 0, 7) - 91) < 1e-4,
          "edited voxel seam height");
    for (std::size_t i = 0; i < seam->indices.size(); i += 3) {
        auto a = seam->heightVertices[seam->indices[i]],
             b = seam->heightVertices[seam->indices[i + 1]],
             c = seam->heightVertices[seam->indices[i + 2]];
        double nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        double ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        double nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        double outward = a.face == 0   ? -nx
                         : a.face == 1 ? nx
                         : a.face == 2 ? ny
                         : a.face == 4 ? -nz
                                       : nz;
        check(outward >= -1e-6, "heightmap closure face winding");
    }
}
void metadata() {
    auto dir =
        std::filesystem::path("testing") /
        ("v135-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto s = WorldSettings::terrainFields("rolling-hills");
    s.seed = UINT64_MAX;
    s.splineY[2] = 73.123456789123;
    s.save(dir / "world.meta");
    auto loaded = WorldSettings::load(dir / "world.meta");
    check(loaded.splineY == s.splineY && loaded.seed == s.seed && loaded.preset == s.preset,
          "schema2 round trip");
    rejects([&] { WorldSettings::open(dir, {}, std::string("flat")); });
    rejects([&] { WorldSettings::open(dir, {}, {}, false); });
    auto flat = WorldSettings::open(dir / "flat", 0, std::string("flat"));
    check(!flat.caves, "flat caves default");
    auto legacy = dir / "legacy";
    WorldSettings{}.save(legacy / "world.meta");
    auto read = [&] {
        std::ifstream f(legacy / "world.meta");
        std::stringstream out;
        out << f.rdbuf();
        return out.str();
    };
    auto original = read();
    check(WorldSettings::open(legacy).generatorVersion == "simplex-1", "legacy load");
    check(original == read(), "legacy metadata rewritten");
    auto invalid = s;
    invalid.maskHigh = invalid.maskLow;
    rejects([&] { invalid.validate(); });
    invalid = s;
    invalid.splineX[2] = invalid.splineX[1];
    rejects([&] { invalid.validate(); });
    std::filesystem::remove_all(dir);
}
void stream() {
#ifdef NDEBUG
    constexpr int waitSeconds = 45 * BLOXIDE_TEST_TIMEOUT_SCALE;
#else
    constexpr int waitSeconds = 180 * BLOXIDE_TEST_TIMEOUT_SCALE;
#endif
    auto s = WorldSettings::terrainFields("flat");
    World world(s, true);
    auto p = WorldPosition::fromAbsolute(8, 80, 8);
    std::unordered_map<ChunkCoord, std::shared_ptr<const ChunkMesh>, ChunkCoordHash> gpu;
    auto tick = [&] {
        world.updateAt(p, {0, 0, -1});
        for (auto& u : world.takePendingUploads())
            world.markUploaded(u);
        for (auto& g : world.readyGroups()) {
            std::vector<ChunkCoord> coords;
            for (auto& u : g.members) {
                check(std::none_of(coords.begin(), coords.end(),
                                   [&](ChunkCoord c) { return c == u.coord; }),
                      "publication contains duplicate GPU coordinate");
                coords.push_back(u.coord);
            }
            check(world.commitGroup(g), "publish");
            for (auto& u : g.members)
                gpu[u.coord] = u.mesh;
        }
        for (auto& r : world.renderChunksAt(p, {0, 0, 0})) {
            auto it = gpu.find(r.coord);
            if (it == gpu.end() || it->second->instanceId != r.mesh->instanceId ||
                it->second->revision != r.mesh->revision)
                throw std::runtime_error(
                    "GPU ownership mismatch coord=" + std::to_string(r.coord.x) + "," +
                    std::to_string(r.coord.z) + " center=" + std::to_string(p.chunk.x) + "," +
                    std::to_string(p.chunk.z) + " desired=" + std::to_string(r.mesh->heightmap) +
                    ":" + std::to_string(r.mesh->instanceId) + ":" +
                    std::to_string(r.mesh->revision) + " gpu=" +
                    (it == gpu.end() ? "missing"
                                     : std::to_string(it->second->heightmap) + ":" +
                                           std::to_string(it->second->instanceId) + ":" +
                                           std::to_string(it->second->revision)));
        }
    };
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
    for (;;) {
        tick();
        auto rendered = world.renderChunksAt(p, {0, 0, 0});
        if (rendered.size() == 2401)
            break;
        check(std::chrono::steady_clock::now() < deadline, "LOD cold stream timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(world.heightmapTiles() == 2112, "far tile count");
    check(world.stats().loaded == 441, "far terrain allocated voxel records");
    check(!world.columnLoaded(16 * 20, 0), "far terrain became collidable");
    auto before = world.stats().generationJobs;
    check(world.set(8, 64, 8, BlockType::Air), "edit");
    for (int i = 0; i < 150; ++i) {
        tick();
        if (world.targetable(8, 8))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(world.stats().generationJobs == before, "edit regenerated terrain");
    for (int x : {16, 32, 16, 0, -16, 0}) {
        p = WorldPosition::fromAbsolute(x + 8, 80, 8);
        auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds);
        do {
            tick();
            if (world.heightmapSettled() && world.stats().ready == 289 &&
                world.renderChunksAt(p, {0, 0, 0}).size() == 2401)
                break;
            check(std::chrono::steady_clock::now() < limit, "handoff/reversal did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (true);
    }
    p = WorldPosition::fromAbsolute(1024, 80, -1024);
    tick();
    for (int i = 0; i < 8; ++i) {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (auto& r : world.renderChunksAt(p, {0, 0, 0}))
        check(std::max(std::llabs(r.coord.x - p.chunk.x), std::llabs(r.coord.z - p.chunk.z)) <= 24,
              "distance culling");
}
int main() {
    try {
        generation();
        mesh();
        metadata();
        stream();
        std::cout
            << "v1.3.5 fields, optimized fill, metadata, LOD seams/morph and streaming passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
