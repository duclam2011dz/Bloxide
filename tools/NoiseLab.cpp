#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/Noise.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
using namespace bloxide;
using Clock = std::chrono::steady_clock;
std::uint64_t hash(const ChunkData& data) {
    std::uint64_t h = 1469598103934665603ULL;
    for (auto b : data.blocks) {
        h ^= b.value & 255;
        h *= 1099511628211ULL;
        h ^= b.value >> 8;
        h *= 1099511628211ULL;
    }
    return h;
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}
int main(int argc, char** argv) {
    try {
        bool quick = false;
        unsigned repeat = 5;
        std::uint64_t seed = 130;
        std::filesystem::path output = "build/benchmark/v1.4-noise";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help") {
                std::cout << "BloxideNoiseLab [--quick] [--repeat 1..20] [--seed UINT64] [--output "
                             "DIRECTORY]\nQuick runs never qualify SIMD.\n";
                return 0;
            } else if (a == "--quick")
                quick = true;
            else if (a == "--seed" && i + 1 < argc)
                seed = WorldSettings::parseSeed(argv[++i]);
            else if (a == "--repeat" && i + 1 < argc) {
                auto n = WorldSettings::parseSeed(argv[++i]);
                if (n < 1 || n > 20)
                    throw std::invalid_argument("repeat 1..20");
                repeat = unsigned(n);
            } else if (a == "--output" && i + 1 < argc)
                output = argv[++i];
            else
                throw std::invalid_argument(
                    "NoiseLab [--quick] [--repeat N] [--seed uint64] [--output directory]");
        }
        std::filesystem::create_directories(output);
        std::ofstream csv(output / "samples.csv"), json(output / "summary.json");
        csv << "backend,repeat,ns_per_sample_2d,samples_per_second_2d,ns_per_sample_3d,samples_per_"
               "second_3d,worldgen_p95_ms,worldgen_p99_ms\n";
        json << "{\"seed\":\"" << seed << "\",\"compiler\":\"" << __VERSION__
             << "\",\"build_type\":\"" << BLOXIDE_BENCHMARK_BUILD_TYPE
             << "\",\"optimization\":" << BLOXIDE_BENCHMARK_OPT_LEVEL << ",\"backends\":[";
        auto settings = WorldSettings::densityWorld();
        settings.seed = seed;
        SimplexNoise2D n2(seed);
        SimplexNoise3D n3(seed);
        NoiseGrid2D g2{-500.2, 399.3, 0.0137, 0.0171, 256, 256};
        NoiseGrid3D g3{-500.2, 12.3, 399.3, 0.0137, 0.0311, 0.0171, 64, 16, 32};
        std::vector<double> samples(256 * 256), reference(256 * 256);
        std::vector<std::uint64_t> scalarHashes;
        double scalarNs2 = 0, scalarNs3 = 0, scalarGen95 = 0, scalarGen99 = 0;
        bool first = true;
        volatile double sink = 0;
        for (auto backend : {NoiseBackend::Scalar, NoiseBackend::SSE2, NoiseBackend::AVX2}) {
            if (!noiseBackendSupported(backend)) {
                std::cout << noiseBackendName(backend) << " unsupported\n";
                continue;
            }
            bool exact = true;
            std::vector<double> ns2, ns3, gen, gen99;
            n2.sampleGrid2D(g2, reference.data(), reference.size());
            n2.sampleGrid2D(g2, samples.data(), samples.size(), backend);
            exact &= std::memcmp(reference.data(), samples.data(), 256 * 256 * 8) == 0;
            n3.sampleGrid3D(g3, reference.data(), reference.size());
            n3.sampleGrid3D(g3, samples.data(), samples.size(), backend);
            exact &= std::memcmp(reference.data(), samples.data(), 64 * 16 * 32 * 8) == 0;
            ChunkGenerator generator(settings, backend);
            unsigned chunks = quick ? 4 : 32;
            int loops = quick ? 2 : 16;
            for (unsigned r = 0; r < repeat; ++r) {
                auto start = Clock::now();
                for (int i = 0; i < loops; ++i) {
                    n2.sampleGrid2D(g2, samples.data(), samples.size(), backend);
                    sink += samples[(unsigned(i) * 97) % samples.size()];
                }
                double t2 = std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
                            (loops * 256.0 * 256);
                start = Clock::now();
                for (int i = 0; i < loops * 2; ++i) {
                    n3.sampleGrid3D(g3, samples.data(), samples.size(), backend);
                    sink += samples[(unsigned(i) * 103) % (64 * 16 * 32)];
                }
                double t3 = std::chrono::duration<double, std::nano>(Clock::now() - start).count() /
                            (loops * 2 * 64.0 * 16 * 32);
                std::vector<double> chunkTimes;
                for (unsigned i = 0; i < chunks; ++i) {
                    ChunkCoord c{int(i) - 8, int(r) - 2};
                    start = Clock::now();
                    auto data = generator.generate(c);
                    chunkTimes.push_back(
                        std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                    auto digest = hash(*data);
                    if (backend == NoiseBackend::Scalar)
                        scalarHashes.push_back(digest);
                    else
                        exact &= scalarHashes[r * chunks + i] == digest;
                }
                std::sort(chunkTimes.begin(), chunkTimes.end());
                double p95 = chunkTimes[(chunkTimes.size() - 1) * 95 / 100];
                gen.push_back(p95);
                double p99 = chunkTimes[(chunkTimes.size() - 1) * 99 / 100];
                gen99.push_back(p99);
                ns2.push_back(t2);
                ns3.push_back(t3);
                csv << noiseBackendName(backend) << ',' << r << ',' << t2 << ',' << 1e9 / t2 << ','
                    << t3 << ',' << 1e9 / t3 << ',' << p95 << ',' << p99 << '\n';
            }
            double m2 = median(ns2), m3 = median(ns3), p95 = median(gen);
            double p99 = median(gen99);
            if (backend == NoiseBackend::Scalar) {
                scalarNs2 = m2;
                scalarNs3 = m3;
                scalarGen95 = p95;
                scalarGen99 = p99;
            }
            bool latencyGate = p95 <= scalarGen95 * 1.05 && p99 <= scalarGen99 * 1.05;
            bool releaseEligible = std::strcmp(BLOXIDE_BENCHMARK_BUILD_TYPE, "Release") == 0 &&
                                   BLOXIDE_BENCHMARK_OPT_LEVEL >= 2 &&
                                   BLOXIDE_BENCHMARK_OPT_LEVEL <= 3;
            bool gate2 = releaseEligible && !quick && repeat >= 5 && exact &&
                         scalarNs2 / m2 >= 1.1 && latencyGate;
            bool gate3 = releaseEligible && !quick && repeat >= 5 && exact &&
                         scalarNs3 / m3 >= 1.1 && latencyGate;
            if (!first)
                json << ',';
            first = false;
            json << "{\"backend\":\"" << noiseBackendName(backend)
                 << "\",\"exact\":" << (exact ? "true" : "false") << ",\"median_ns_2d\":" << m2
                 << ",\"median_ns_3d\":" << m3 << ",\"worldgen_p95_ms\":" << p95
                 << ",\"worldgen_p99_ms\":" << p99 << ",\"gate_2d\":" << (gate2 ? "true" : "false")
                 << ",\"gate_3d\":" << (gate3 ? "true" : "false") << '}';
            std::cout << noiseBackendName(backend) << " exact=" << exact << " ns2=" << m2
                      << " ns3=" << m3 << " genp95=" << p95 << " gate2=" << gate2
                      << " gate3=" << gate3 << '\n';
            if (!exact)
                throw std::runtime_error("SIMD changed samples/worldgen");
        }
        json << "],\"production_auto\":\"scalar until explicit qualification\"}\n";
        std::cout << "checksum=" << sink << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
