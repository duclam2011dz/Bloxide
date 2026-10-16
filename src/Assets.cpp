#include <bloxide/Textures.hpp>
#include <lodepng.h>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <cmath>
namespace bloxide {
RenderClass textureRenderClass(unsigned group) {
    if (group >= TextureGroups.size())
        throw std::out_of_range("texture group");
    if (group == 8 || group == 22)
        return RenderClass::Translucent;
    if (group == 11 || group == 15 || group == 19 || group == 25 || group == 26)
        return RenderClass::Cutout;
    return RenderClass::Opaque;
}
std::vector<TextureImage> cutoutMipChain(const TextureImage& source) {
    if (source.width != 32 || source.height != 32 || source.rgba.size() != 4096)
        throw std::invalid_argument("cutout mip source");
    std::size_t covered = 0;
    for (std::size_t i = 3; i < source.rgba.size(); i += 4)
        covered += source.rgba[i] >= 128;
    double coverage = covered / 1024.0;
    std::vector<TextureImage> levels;
    levels.push_back(source);
    while (levels.back().width > 1) {
        auto& previous = levels.back();
        TextureImage mip;
        mip.width = previous.width / 2;
        mip.height = previous.height / 2;
        mip.rgba.assign(std::size_t(mip.width) * mip.height * 4, 0);
        for (unsigned y = 0; y < mip.height; ++y)
            for (unsigned x = 0; x < mip.width; ++x) {
                unsigned alpha = 0;
                double rgb[3]{};
                for (unsigned dy = 0; dy < 2; ++dy)
                    for (unsigned dx = 0; dx < 2; ++dx) {
                        auto p = ((y * 2 + dy) * previous.width + x * 2 + dx) * 4;
                        auto a = previous.rgba[p + 3];
                        alpha += a;
                        for (int c = 0; c < 3; ++c)
                            rgb[c] += previous.rgba[p + c] * double(a);
                    }
                auto p = (y * mip.width + x) * 4;
                for (int c = 0; c < 3; ++c)
                    mip.rgba[p + c] = static_cast<unsigned char>(
                        alpha ? std::clamp(int(std::lround(rgb[c] / alpha)), 0, 255) : 0);
                mip.rgba[p + 3] = static_cast<unsigned char>(alpha / 4);
            }
        std::vector<unsigned> order(std::size_t(mip.width) * mip.height);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](unsigned a, unsigned b) {
            return mip.rgba[a * 4 + 3] > mip.rgba[b * 4 + 3];
        });
        auto desired = std::min(order.size(), std::size_t(std::lround(coverage * order.size())));
        for (std::size_t rank = 0; rank < order.size(); ++rank) {
            auto& alpha = mip.rgba[order[rank] * 4 + 3];
            alpha = rank < desired ? std::max<unsigned char>(alpha, 129)
                                   : std::min<unsigned char>(alpha, 127);
        }
        levels.push_back(std::move(mip));
    }
    return levels;
}
unsigned textureBaseLayer(BlockType b, int face) {
    switch (b) {
    case BlockType::Grass:
        return face == 2 ? 0 : face == 3 ? 8 : 4;
    case BlockType::Dirt:
        return 8;
    case BlockType::Stone:
        return 12;
    case BlockType::Bedrock:
        return 16;
    case BlockType::IronOre:
        return 20;
    case BlockType::GoldOre:
        return 24;
    case BlockType::DiamondOre:
        return 28;
    case BlockType::Water:
        return 32;
    case BlockType::OakLog:
        return (face == 2 || face == 3) ? 40 : 36;
    case BlockType::OakLeaves:
        return 44;
    case BlockType::OakPlanks:
        return 48;
    case BlockType::BirchLog:
        return (face == 2 || face == 3) ? 56 : 52;
    case BlockType::BirchLeaves:
        return 60;
    case BlockType::BirchPlanks:
        return 64;
    case BlockType::PineLog:
        return (face == 2 || face == 3) ? 72 : 68;
    case BlockType::PineLeaves:
        return 76;
    case BlockType::PinePlanks:
        return 80;
    case BlockType::Sand:
        return 84;
    case BlockType::Glass:
        return 88;
    case BlockType::Bricks:
        return 92;
    case BlockType::SlateRoof:
        return 96;
    case BlockType::RedFlower:
        return 100;
    case BlockType::YellowFlower:
        return 104;
    case BlockType::Cobblestone:
        return 108;
    default:
        return 0;
    }
}
std::uint32_t textureVariant(std::int64_t x, int y, std::int64_t z, unsigned base,
                             std::uint32_t seed) {
    auto ux = static_cast<std::uint64_t>(x), uz = static_cast<std::uint64_t>(z);
    std::uint32_t h = std::uint32_t(ux) * 73856093U ^ std::uint32_t(ux >> 32) * 0x52dce729U ^
                      std::uint32_t(y) * 19349663U ^ std::uint32_t(uz) * 83492791U ^
                      std::uint32_t(uz >> 32) * 0x38495ab5U ^ base * 2654435761U ^ seed;
    h ^= h >> 16U;
    h *= 0x7feb352dU;
    h ^= h >> 15U;
    h *= 0x846ca68bU;
    h ^= h >> 16U;
    return h & 3U;
}
TextureImage AssetLoader::loadTexture(const std::filesystem::path& file, RenderClass policy) {
    auto fail = [&](const std::string& s) { throw std::runtime_error(file.string() + ": " + s); };
    if (!std::filesystem::is_regular_file(file))
        fail("PNG file missing");
    if (std::filesystem::file_size(file) > 1024 * 1024)
        fail("PNG file too large");
    std::vector<unsigned char> bytes;
    auto error = lodepng::load_file(bytes, file.string());
    if (error)
        fail(lodepng_error_text(error));
    lodepng::State state;
    unsigned w = 0, h = 0;
    error = lodepng_inspect(&w, &h, &state, bytes.data(), bytes.size());
    if (error)
        fail(lodepng_error_text(error));
    if (w != 32 || h != 32)
        fail("expected 32x32");
    if (state.info_png.color.colortype != LCT_RGBA || state.info_png.color.bitdepth != 8)
        fail("source PNG must be RGBA8, not RGB/indexed/16-bit");
    TextureImage image;
    image.rgba.clear();
    state.decoder.zlibsettings.max_output_size = 8192;
    error = lodepng::decode(image.rgba, image.width, image.height, state, bytes);
    if (error)
        fail(lodepng_error_text(error));
    if (image.rgba.size() != 4096)
        fail("decoded RGBA byte count invalid");
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        if (policy == RenderClass::Opaque && image.rgba[i] != 255)
            fail("opaque block texture requires alpha=255");
        if (policy == RenderClass::Cutout && image.rgba[i] != 0 && image.rgba[i] != 255)
            fail("cutout source alpha must be 0 or 255");
        if (policy == RenderClass::Translucent && image.rgba[i] == 255)
            fail("translucent source requires alpha below 255");
    }
    return image;
}
TextureAssets AssetLoader::load(const std::filesystem::path& root) {
    auto start = std::chrono::steady_clock::now();
    std::ifstream manifest(root / "manifest.txt");
    if (!manifest)
        throw std::runtime_error((root / "manifest.txt").string() + ": missing manifest");
    std::string magic, version;
    unsigned schema = 0, w = 0, h = 0, count = 0;
    std::uint64_t seed = 0;
    if (!(manifest >> magic >> schema >> w >> h >> count >> seed >> version) ||
        magic != "BLOXIDE_TEXTURES" || w != 32 || h != 32 ||
        !((schema == 1 && count == 20) || (schema == 2 && count == TextureLayers)) ||
        seed > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error((root / "manifest.txt").string() + ": invalid header");
    TextureAssets assets;
    assets.seed = static_cast<std::uint32_t>(seed);
    assets.layerCount = count;
    std::array<bool, TextureLayers> seen{};
    for (unsigned i = 0; i < count; ++i) {
        unsigned layer = 0, variant = 0;
        std::string group, filename;
        if (!(manifest >> layer >> group >> variant >> filename) || layer >= count || seen[layer] ||
            variant != layer % 4 || group != TextureGroups[layer / 4] ||
            filename != group + "_" + std::to_string(variant) + ".png")
            throw std::runtime_error((root / "manifest.txt").string() +
                                     ": invalid/duplicate layer entry " + std::to_string(i));
        auto policy = textureRenderClass(layer / 4);
        if (schema == 2) {
            unsigned declared = 0;
            if (!(manifest >> declared) || declared != unsigned(policy))
                throw std::runtime_error((root / "manifest.txt").string() +
                                         ": invalid render class");
        }
        seen[layer] = true;
        assets.layers[layer] = loadTexture(root / filename, policy);
        assets.pngBytes += std::filesystem::file_size(root / filename);
    }
    std::string extra;
    if (manifest >> extra)
        throw std::runtime_error((root / "manifest.txt").string() + ": unexpected extra entries");
    assets.loadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return assets;
}
} // namespace bloxide
