#pragma once
#include <bloxide/Block.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace bloxide {
using Pixel=std::array<unsigned char,4>;
using Palette=std::vector<Pixel>;
struct TextureImage {
    unsigned width=32,height=32;
    std::vector<unsigned char> rgba=std::vector<unsigned char>(32*32*4,255);
};
constexpr unsigned TextureSize=32,TextureLayers=112,TextureVariants=4;
inline constexpr std::array<const char*,28> TextureGroups{"grass_top","grass_side","dirt","stone","bedrock",
    "iron_ore","gold_ore","diamond_ore","water","oak_log_side","oak_log_top","oak_leaves","oak_planks",
    "birch_log_side","birch_log_top","birch_leaves","birch_planks","pine_log_side","pine_log_top","pine_leaves","pine_planks",
    "sand","glass","bricks","slate_roof","red_flower","yellow_flower","cobblestone"};
RenderClass textureRenderClass(unsigned group);
std::vector<TextureImage> cutoutMipChain(const TextureImage&);
unsigned textureBaseLayer(BlockType block,int face);
std::uint32_t textureVariant(std::int64_t x,int y,std::int64_t z,unsigned baseLayer,std::uint32_t seed);
struct TextureAssets {
    std::array<TextureImage,TextureLayers> layers;
    std::uint32_t seed=125;
    unsigned layerCount=TextureLayers;
    double loadMs=0;
    std::uintmax_t pngBytes=0;
};
class AssetLoader {
public:
    static TextureImage loadTexture(const std::filesystem::path& filename,RenderClass policy=RenderClass::Opaque);
    static TextureAssets load(const std::filesystem::path& directory);
};
class TextureGenerator {
public:
    static Palette palette(unsigned group);
    static TextureImage generate(unsigned group,unsigned variant,std::uint32_t seed=125);
    static void writePng(const std::filesystem::path& filename,const TextureImage& image);
    static void exportAssets(const std::filesystem::path& directory,std::uint32_t seed=125);
    static double periodicNoise(double x,double y,int cells,std::uint32_t seed);
};
}
