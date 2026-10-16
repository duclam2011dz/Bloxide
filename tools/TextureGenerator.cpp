#include <bloxide/Textures.hpp>
#include <chrono>
#include <iostream>
#include <limits>
int main(int argc, char** argv) {
    try {
        std::filesystem::path output = "assets/textures";
        std::uint32_t seed = 125;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--output" && i + 1 < argc)
                output = argv[++i];
            else if (arg == "--seed" && i + 1 < argc) {
                std::string value = argv[++i];
                std::size_t used = 0;
                auto n = std::stoull(value, &used);
                if (used != value.size() || n > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error("seed must be uint32");
                seed = static_cast<std::uint32_t>(n);
            } else
                throw std::runtime_error(
                    "usage: BloxideTextureGenerator [--output directory] [--seed uint32]");
        }
        auto start = std::chrono::steady_clock::now();
        bloxide::TextureGenerator::exportAssets(output, seed);
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        auto assets = bloxide::AssetLoader::load(output);
        std::cout << "generated_layers=" << assets.layerCount << " seed=" << seed
                  << " generation_ms=" << ms << " load_ms=" << assets.loadMs
                  << " png_bytes=" << assets.pngBytes << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
