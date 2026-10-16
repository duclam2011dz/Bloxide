#include <bloxide/GameApplication.hpp>
#include <bloxide/BuildInfo.hpp>
#include <bloxide/RuntimePaths.hpp>
#include <bloxide/Textures.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace bloxide {
ApplicationOptions parseApplicationOptions(const std::vector<std::string>& args) {
    ApplicationOptions out;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size())
                throw std::invalid_argument("Missing value for " + arg);
            return args[++i];
        };
        if (arg == "--help")
            out.help = true;
        else if (arg == "--version")
            out.version = true;
        else if (arg == "--json")
            out.json = true;
        else if (arg == "--validate-assets")
            out.validateAssets = true;
        else if (arg == "--smoke-test")
            out.smoke = true;
        else if (arg == "--no-lod")
            out.lod = false;
        else if (arg == "--require-lod")
            out.requireLod = true;
        else if (arg == "--benchmark-run")
            out.benchmark.enabled = true;
        else if (arg == "--seed")
            out.seed = WorldSettings::parseSeed(value());
        else if (arg == "--world")
            out.world = std::filesystem::u8path(value());
        else if (arg == "--preset")
            out.preset = value();
        else if (arg == "--benchmark-output")
            out.benchmark.output = std::filesystem::u8path(value());
        else if (arg == "--diagnostics-dir")
            out.benchmark.diagnostics = std::filesystem::u8path(value());
        else if (arg == "--caves") {
            auto v = value();
            if (v != "on" && v != "off")
                throw std::invalid_argument("--caves on|off");
            out.caves = v == "on";
        } else if (arg == "--benchmark-devtools") {
            auto v = value();
            if (v != "on" && v != "off")
                throw std::invalid_argument("--benchmark-devtools on|off");
            out.benchmark.overlay = v == "on";
        } else if (arg == "--far-radius") {
            const auto& v = value();
            std::size_t used = 0;
            out.benchmark.farRadius = std::stoi(v, &used);
            if (used != v.size() || out.benchmark.farRadius < 8 || out.benchmark.farRadius > 24)
                throw std::invalid_argument("--far-radius 8..24");
        } else if (arg == "--warmup" || arg == "--duration") {
            const auto& v = value();
            std::size_t used = 0;
            auto n = std::stod(v, &used);
            if (used != v.size() || !std::isfinite(n) || n < 0 || n > 600 ||
                (arg == "--duration" && n <= 0))
                throw std::invalid_argument("Invalid benchmark time");
            (arg == "--warmup" ? out.benchmark.warmup : out.benchmark.duration) = n;
        } else if (arg == "--smoke-origin") {
            const auto& v = value();
            std::size_t used = 0;
            out.offset = std::stoll(v, &used);
            if (used != v.size())
                throw std::invalid_argument("Invalid smoke origin");
        } else if (arg == "--scenario") {
            out.benchmark.scenario = value();
            const std::vector<std::string> allowed{"cold",    "steady",    "traversal", "reversal",
                                                   "fluid",   "edit",      "budget",    "forest",
                                                   "village", "underwater"};
            if (std::find(allowed.begin(), allowed.end(), out.benchmark.scenario) == allowed.end())
                throw std::invalid_argument("Unknown benchmark scenario");
        } else if (arg == "--noise-backend") {
            auto v = value();
            if (v == "scalar")
                out.benchmark.backend = NoiseBackend::Scalar;
            else if (v == "sse2")
                out.benchmark.backend = NoiseBackend::SSE2;
            else if (v == "avx2")
                out.benchmark.backend = NoiseBackend::AVX2;
            else if (v == "auto")
                out.benchmark.backend = NoiseBackend::Auto;
            else
                throw std::invalid_argument("Unknown noise backend");
        } else
            throw std::invalid_argument("Unknown option: " + arg);
    }
    if (out.help || out.version || out.validateAssets)
        return out;
    if (out.json)
        throw std::invalid_argument("--json requires --version");
    if (out.offset && !out.smoke)
        throw std::invalid_argument("--smoke-origin is test-only");
    if (out.smoke && out.benchmark.enabled)
        throw std::invalid_argument("Smoke and benchmark are separate modes");
    if (out.requireLod && (!out.smoke || !out.lod))
        throw std::invalid_argument("--require-lod needs smoke and LOD");
    if (!out.benchmark.enabled && out.benchmark.backend != NoiseBackend::Auto)
        throw std::invalid_argument("Explicit SIMD backend is benchmark-only");
    if (!noiseBackendSupported(out.benchmark.backend))
        throw std::invalid_argument("Backend not compiled or unsupported by CPU/OS");
    return out;
}
} // namespace bloxide
int main(int argc, char** argv) {
    try {
        auto options = bloxide::parseApplicationOptions(bloxide::platformArguments(argc, argv));
        if (options.version) {
            std::cout << (options.json ? bloxide::buildInfoJson() : bloxide::versionText()) << '\n';
            return 0;
        }
        if (options.validateAssets) {
            auto assets =
                bloxide::AssetLoader::load(bloxide::executableDirectory() / "assets/textures");
            std::cout << "Validated " << assets.layers.size() << " texture layers\n";
            return 0;
        }
        if (options.help) {
            std::cout
                << "Bloxide --version [--json]\n"
                << "Bloxide [--world DIR] [--seed UINT64] [--preset "
                   "flat|rolling-hills|mountainous]\n"
                << "  [--caves on|off] [--no-lod] [--far-radius 8..24] [--diagnostics-dir DIR]\n"
                << "  --benchmark-run [--scenario NAME] [--warmup SECONDS] [--duration SECONDS]\n"
                << "  [--benchmark-output DIR] [--benchmark-devtools on|off] [--noise-backend "
                   "scalar|sse2|avx2|auto]\n"
                << "Only world metadata persists. Edits/fluid changes remain RAM-only.\n";
            return 0;
        }
        if (options.benchmark.diagnostics.empty())
            options.benchmark.diagnostics =
                bloxide::diagnosticDirectory(options.smoke ? "tests" : "game");
        if (options.benchmark.enabled) {
            if (options.benchmark.output.empty())
                options.benchmark.output = bloxide::diagnosticDirectory("benchmark");
            if (options.world.empty())
                options.world = options.benchmark.output / "world";
            if (!options.seed)
                options.seed = 130;
        }
        if (options.world.empty()) {
            options.world = options.smoke ? options.benchmark.diagnostics / "world"
                                          : bloxide::userDataDirectory() / "worlds/default";
        }
        if (options.smoke && !options.seed)
            options.seed = 130;
        auto settings = bloxide::WorldSettings::open(options.world, options.seed, options.preset,
                                                     options.caves);
        std::cout << "world_seed=" << settings.seed << " generator=" << settings.generatorVersion
                  << " metadata=" << (options.world / "world.meta").u8string() << '\n';
        return bloxide::runGameApplication(settings, options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
