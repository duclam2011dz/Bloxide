#include <bloxide/World.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
using namespace bloxide;
std::uint64_t chunkHash(const ChunkData& data, bool states = false) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto b : data.blocks) {
        h ^= static_cast<unsigned char>(b);
        h *= 1099511628211ULL;
        if (states) {
            h ^= b.value >> 8;
            h *= 1099511628211ULL;
        }
    }
    return h;
}
int main(int argc, char** argv) {
    try {
        bool probe = false, reverse = false, threads = false, golden = false, quick = false;
        WorldSettings settings;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--probe")
                probe = true;
            else if (arg == "--reverse")
                reverse = true;
            else if (arg == "--threads")
                threads = true;
            else if (arg == "--golden")
                golden = true;
            else if (arg == "--quick")
                quick = true;
            else if (arg == "--fields") {
                auto seed = settings.seed;
                settings = WorldSettings::terrainFields();
                settings.seed = seed;
            } else if (arg == "--density") {
                auto seed = settings.seed;
                settings = WorldSettings::densityWorld();
                settings.seed = seed;
            } else if (arg == "--seed" && i + 1 < argc)
                settings.seed = WorldSettings::parseSeed(argv[++i]);
            else
                throw std::invalid_argument("invalid benchmark argument");
        }
        if (golden) {
            SimplexNoise2D n2(42);
            SimplexNoise3D n3(42);
            std::cout << std::setprecision(17);
            std::cout << n2.sample(0.1, 0.2) << ' ' << n2.sample(-0.25, 0.75) << ' '
                      << n2.sample(100.25, -77.5) << '\n';
            std::cout << n3.sample(0.1, 0.2, 0.3) << ' ' << n3.sample(-0.25, 0.75, -1.25) << ' '
                      << n3.sample(100.25, -77.5, 33.25) << '\n';
            return 0;
        }
        ChunkGenerator generator(settings);
        std::vector<ChunkCoord> coords{
            {0, 0}, {-1, 2}, {3, -2}, {WorldLimit / 16 - 16, -WorldLimit / 16 + 16}};
        if (reverse)
            std::reverse(coords.begin(), coords.end());
        if (probe) {
            std::vector<std::pair<ChunkCoord, std::uint64_t>> hashes;
            if (threads) {
                std::vector<std::future<std::uint64_t>> work;
                for (auto c : coords)
                    work.push_back(std::async(std::launch::async, [&, c] {
                        return chunkHash(*generator.generate(c),
                                         settings.generatorVersion == "density-world-1");
                    }));
                for (std::size_t i = 0; i < coords.size(); ++i)
                    hashes.push_back({coords[i], work[i].get()});
            } else
                for (auto c : coords)
                    hashes.push_back(
                        {c, chunkHash(*generator.generate(c),
                                      settings.generatorVersion == "density-world-1")});
            std::sort(hashes.begin(), hashes.end(), [](auto& a, auto& b) {
                return a.first.x != b.first.x ? a.first.x < b.first.x : a.first.z < b.first.z;
            });
            for (auto& [c, h] : hashes)
                std::cout << c.x << ',' << c.z << ':' << h << '\n';
            return 0;
        }
        using Clock = std::chrono::steady_clock;
        std::filesystem::create_directories("build/benchmark");
        std::ofstream out("build/benchmark/v1.3-noise.csv");
        out << "metric,value,unit\n";
        auto metric = [&](const char* name, double value, const char* unit) {
            out << name << ',' << value << ',' << unit << '\n';
            std::cout << name << '=' << value << ' ' << unit << '\n';
        };
        SimplexNoise2D n2(settings.seed);
        SimplexNoise3D n3(settings.seed);
        const int samples = quick ? 10000 : 1000000;
        volatile double sink = 0;
        auto start = Clock::now();
        for (int i = 0; i < samples; ++i)
            sink += n2.sample(i * 0.013, -i * 0.017);
        metric("simplex2D",
               std::chrono::duration<double, std::nano>(Clock::now() - start).count() / samples,
               "ns/sample");
        start = Clock::now();
        for (int i = 0; i < samples; ++i)
            sink += n3.sample(i * 0.013, -i * 0.017, i * 0.019);
        metric("simplex3D",
               std::chrono::duration<double, std::nano>(Clock::now() - start).count() / samples,
               "ns/sample");
        FractalSettings f{6, 1.0 / 64, 2, 0.5};
        for (int footprint : {1, 4, 16}) {
            start = Clock::now();
            for (int i = 0; i < samples / 10; ++i)
                sink += FractalNoise::sample2D(n2, f, i, -i, FractalMode::Fbm, footprint);
            std::string name = "fbm_footprint_" + std::to_string(footprint);
            metric(name.c_str(),
                   std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
                       (samples / 10),
                   "ns/sample");
        }
        double genTotal = 0, meshTotal = 0;
        std::size_t meshBytes = 0, air = 0;
        ChunkMesher mesher;
        for (auto c : coords) {
            start = Clock::now();
            auto data = generator.generate(c);
            genTotal += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            for (auto b : data->blocks)
                air += b == BlockType::Air;
            start = Clock::now();
            auto mesh = mesher.build(c, data, {}, 0);
            meshTotal += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            meshBytes += mesh->byteSize();
        }
        metric("generation_average", genTotal / coords.size(), "ms/chunk");
        metric("meshing_average", meshTotal / coords.size(), "ms/chunk");
        metric("mesh_payload", double(meshBytes), "bytes");
        World world(settings);
        WorldPosition position = WorldPosition::fromAbsolute(8, 128, 8);
        double maxDriver = 0;
        auto tick = [&] {
            auto t = Clock::now();
            world.updateAt(position, {0, 0, -1});
            for (auto& u : world.takePendingUploads())
                world.markUploaded(u);
            for (auto& g : world.readyGroups())
                world.commitGroup(g);
            maxDriver = std::max(
                maxDriver, std::chrono::duration<double, std::milli>(Clock::now() - t).count());
        };
        auto settled = [&] {
            if (world.renderChunksAt(position, {0, 0, 0}).size() != 289)
                return false;
            for (int z = -10; z <= 10; ++z)
                for (int x = -10; x <= 10; ++x)
                    if (!world.columnLoaded((position.chunk.x + x) * 16,
                                            (position.chunk.z + z) * 16))
                        return false;
            return true;
        };
        start = Clock::now();
        auto deadline = start + std::chrono::seconds(60);
        while (!settled() && Clock::now() < deadline) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!settled())
            throw std::runtime_error("cold stream timeout");
        metric("cold_stream",
               std::chrono::duration<double, std::milli>(Clock::now() - start).count(), "ms");
        position = WorldPosition::fromAbsolute(72, 128, 8);
        start = Clock::now();
        deadline = start + std::chrono::seconds(60);
        tick();
        while (!settled() && Clock::now() < deadline) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!settled())
            throw std::runtime_error("traversal timeout");
        metric("traversal_settle",
               std::chrono::duration<double, std::milli>(Clock::now() - start).count(), "ms");
        int y = 255;
        while (y > 0 && !isSolid(world.get(72, y, 8)))
            --y;
        if (!world.set(72, y, 8, BlockType::Air))
            throw std::runtime_error("benchmark edit rejected");
        start = Clock::now();
        deadline = start + std::chrono::seconds(60);
        while (!world.targetable(72, 8) && Clock::now() < deadline) {
            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!world.targetable(72, 8))
            throw std::runtime_error("edit timeout");
        metric("edit_publish", world.stats().lastPublishMs, "ms");
        metric("driver_max", maxDriver, "ms");
        auto s = world.stats();
        metric("loaded_chunks", double(s.loaded), "chunks");
        metric("active_mesh_bytes", double(s.vertices * 4 + s.indices * 4), "bytes");
        std::cout << "checksum=" << sink << " seed=" << settings.seed
                  << " stale_results=" << s.staleResults << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
