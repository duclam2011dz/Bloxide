#include <bloxide/Textures.hpp>
#include <lodepng.h>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace bloxide {
unsigned textureBaseLayer(BlockType b,int face) {
    switch(b){
    case BlockType::Grass:return face==2?0:face==3?8:4;
    case BlockType::Dirt:return 8;
    case BlockType::Stone:return 12;
    case BlockType::Bedrock:return 16;
    default:return 0;
    }
}
std::uint32_t textureVariant(int x,int y,int z,unsigned base,std::uint32_t seed){
    std::uint32_t h=std::uint32_t(x)*73856093U^std::uint32_t(y)*19349663U^std::uint32_t(z)*83492791U^base*2654435761U^seed;
    h^=h>>16U;h*=0x7feb352dU;h^=h>>15U;h*=0x846ca68bU;h^=h>>16U;
    return h&3U;
}
TextureImage AssetLoader::loadTexture(const std::filesystem::path& file) {
    auto fail=[&](const std::string& s){throw std::runtime_error(file.string()+": "+s);};
    if(!std::filesystem::is_regular_file(file))fail("PNG file missing");
    if(std::filesystem::file_size(file)>1024*1024)fail("PNG file too large");
    std::vector<unsigned char> bytes;
    auto error=lodepng::load_file(bytes,file.string());
    if(error)fail(lodepng_error_text(error));
    lodepng::State state;
    unsigned w=0,h=0;
    error=lodepng_inspect(&w,&h,&state,bytes.data(),bytes.size());
    if(error)fail(lodepng_error_text(error));
    if(w!=32||h!=32)fail("expected 32x32");
    if(state.info_png.color.colortype!=LCT_RGBA||state.info_png.color.bitdepth!=8)fail("source PNG must be RGBA8, not RGB/indexed/16-bit");
    TextureImage image;
    image.rgba.clear();
    state.decoder.zlibsettings.max_output_size=8192;
    error=lodepng::decode(image.rgba,image.width,image.height,state,bytes);
    if(error)fail(lodepng_error_text(error));
    if(image.rgba.size()!=4096)fail("decoded RGBA byte count invalid");
    for(std::size_t i=3;i<image.rgba.size();i+=4)if(image.rgba[i]!=255)fail("opaque block texture requires alpha=255");
    return image;
}
TextureAssets AssetLoader::load(const std::filesystem::path& root) {
    auto start=std::chrono::steady_clock::now();
    std::ifstream manifest(root/"manifest.txt");
    if(!manifest)throw std::runtime_error((root/"manifest.txt").string()+": missing manifest");
    std::string magic,version;
    unsigned schema=0,w=0,h=0,count=0;
    std::uint64_t seed=0;
    if(!(manifest>>magic>>schema>>w>>h>>count>>seed>>version)||magic!="BLOXIDE_TEXTURES"||schema!=1||w!=32||h!=32||count!=20||seed>std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error((root/"manifest.txt").string()+": invalid header");
    TextureAssets assets;assets.seed=static_cast<std::uint32_t>(seed);
    std::array<bool,20> seen{};
    for(unsigned i=0;i<20;++i){
        unsigned layer=0,variant=0;std::string group,filename;
        if(!(manifest>>layer>>group>>variant>>filename)||layer>=20||seen[layer]||variant!=layer%4||group!=TextureGroups[layer/4]||filename!=group+"_"+std::to_string(variant)+".png")
            throw std::runtime_error((root/"manifest.txt").string()+": invalid/duplicate layer entry "+std::to_string(i));
        seen[layer]=true;assets.layers[layer]=loadTexture(root/filename);
        assets.pngBytes+=std::filesystem::file_size(root/filename);
    }
    std::string extra;if(manifest>>extra)throw std::runtime_error((root/"manifest.txt").string()+": unexpected extra entries");
    assets.loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return assets;
}
}
