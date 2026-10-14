#include <bloxide/Textures.hpp>
#include <bloxide/Physics.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <lodepng.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <algorithm>
#include <stdexcept>
using namespace bloxide;
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void physics(){
    const auto floor=[](int,int y,int){return y<=0;};
    const auto grass=[](int,int,int){return BlockType::Grass;};
    PlayerPhysics p;p.feet={0.5f,1,0.5f};p.grounded=true;
    p.step(0.02f,1,0,false,floor,grass);
    require(p.velocity[0]>0.8f&&p.velocity[0]<1.0f,"ground acceleration");
    for(int i=0;i<30;++i)p.step(0.01f,1,0,false,floor,grass);
    require(std::abs(p.velocity[0]-5)<0.001f,"maximum walk speed");
    p.step(0.02f,-1,0,false,floor,grass);
    require(p.velocity[0]>3.9f&&p.velocity[0]<4.3f,"bounded reversal");
    p.velocity={5,0,0};p.grounded=true;
    p.step(0.01f,0,0,true,floor,grass);
    require(p.velocity[0]>4.9f&&p.velocity[1]>0&&!p.grounded,"jump preserves momentum");
    PlayerPhysics air;air.feet={0.5f,20,0.5f};air.step(0.02f,1,0,false,floor,grass);
    require(air.velocity[0]>0.1f&&air.velocity[0]<0.3f,"weak air control");
    air.velocity={5,0,0};air.step(0.1f,0,0,false,floor,grass);
    require(std::abs(air.velocity[0]-5*std::exp(-0.015f))<0.001f,"air drag");
    auto brake=[&](BlockType material){
        PlayerPhysics body;body.feet={0.5f,1,0.5f};body.velocity={5,0,0};body.grounded=true;
        body.step(0.1f,0,0,false,floor,[&](int,int,int){return material;});
        return body;
    };
    auto g=brake(BlockType::Grass),s=brake(BlockType::Stone),d=brake(BlockType::Dirt),b=brake(BlockType::Bedrock);
    require(g.velocity[0]<d.velocity[0]&&d.velocity[0]<b.velocity[0]&&b.velocity[0]<s.velocity[0],"material friction order");
    require(std::abs(g.velocity[0]-5*std::exp(-1.4f))<0.001f,"exponential ground friction");
    PlayerPhysics mixed;mixed.feet={1,1,0.5f};mixed.velocity={5,0,0};mixed.grounded=true;
    mixed.step(0.02f,0,0,false,floor,[](int x,int,int){return x==0?BlockType::Grass:BlockType::Stone;});
    require(std::abs(mixed.velocity[0]-5*std::exp(-0.28f))<0.001f,"mixed support strongest friction");
    PlayerPhysics diagonal;diagonal.feet={0.5f,1,0.5f};diagonal.grounded=true;
    for(int i=0;i<120;++i)diagonal.step(1.0f/120,1,1,false,floor,grass);
    require(std::abs(std::hypot(diagonal.velocity[0],diagonal.velocity[2])-5)<0.001f,"diagonal speed");
    std::vector<float> endPositions;
    for(int fps:{30,60,144}){
        PlayerPhysics body;body.feet={0.5f,1,0.5f};body.grounded=true;
        for(int i=0;i<fps;++i)body.step(1.0f/fps,1,0,false,floor,grass);
        for(int i=0;i<fps/2;++i)body.step(1.0f/fps,0,0,false,floor,grass);
        require(body.velocity[0]==0,"stop-speed cutoff");endPositions.push_back(body.feet[0]);
    }
    require(*std::max_element(endPositions.begin(),endPositions.end())-*std::min_element(endPositions.begin(),endPositions.end())<0.02f,"frame-rate independent acceleration/braking");
}
template<class Fn> void fails(Fn fn,const std::string& name){
    try{fn();}catch(const std::exception&e){require(std::string(e.what()).find(name)!=std::string::npos,"asset error includes filename");return;}
    throw std::runtime_error("invalid asset was accepted: "+name);
}
void fixture(const std::filesystem::path& file,LodePNGColorType type,unsigned depth,unsigned w=32,unsigned h=32){
    lodepng::State state;state.encoder.auto_convert=0;
    state.info_raw.colortype=state.info_png.color.colortype=type;
    state.info_raw.bitdepth=state.info_png.color.bitdepth=depth;
    if(type==LCT_PALETTE){
        lodepng_palette_add(&state.info_raw,40,80,120,255);lodepng_palette_add(&state.info_png.color,40,80,120,255);
    }
    std::size_t channels=type==LCT_RGBA?4:type==LCT_RGB?3:1;
    std::vector<unsigned char> raw(std::size_t(w)*h*channels*(depth/8),type==LCT_PALETTE?0:255),encoded;
    require(lodepng::encode(encoded,raw,w,h,state)==0,"fixture encode");
    require(lodepng::save_file(encoded,file.string())==0,"fixture save");
}
void textures(){
    for(unsigned group=0;group<5;++group)for(unsigned variant=0;variant<4;++variant){
        auto image=TextureGenerator::generate(group,variant);
        require(image.rgba==TextureGenerator::generate(group,variant).rgba,"texture seed deterministic");
        require(image.rgba!=TextureGenerator::generate(group,variant,126).rgba,"seed variation");
        require(image.rgba!=TextureGenerator::generate(group,(variant+1)%4).rgba,"variant differs");
        auto palette=TextureGenerator::palette(group);
        require(palette.size()>=8&&palette.size()<=12,"palette size");
        for(std::size_t i=0;i<image.rgba.size();i+=4){
            Pixel color{image.rgba[i],image.rgba[i+1],image.rgba[i+2],image.rgba[i+3]};
            require(std::find(palette.begin(),palette.end(),color)!=palette.end(),"palette quantization");
        }
        for(int p=0;p<32;++p)for(int c=0;c<4;++c){
            require(image.rgba[(p*32)*4+c]==image.rgba[(p*32+31)*4+c],"horizontal tile seam");
            if(group!=1)require(image.rgba[p*4+c]==image.rgba[(31*32+p)*4+c],"vertical tile seam");
        }
        auto other=TextureGenerator::generate(group,(variant+1)%4);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)if(x<2||x>=30||(group!=1&&(y<2||y>=30)))
            for(int c=0;c<4;++c)require(image.rgba[(y*32+x)*4+c]==other.rgba[(y*32+x)*4+c],"cross-variant border compatibility");
    }
    require(std::abs(TextureGenerator::periodicNoise(-0.3,0.27,8,125)-TextureGenerator::periodicNoise(0.7,1.27,8,125))<1e-12,"periodic noise domain");
    auto committed=AssetLoader::load(BLOXIDE_ASSET_DIR);
    for(int i=0;i<20;++i)require(committed.layers[i].rgba==TextureGenerator::generate(i/4,i%4).rgba,"committed assets match generator");
    auto dir=std::filesystem::path("testing")/("v125-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    auto missing=dir/"missing.png";fails([&]{AssetLoader::loadTexture(missing);},"missing.png");
    auto corrupt=dir/"corrupt.png";{std::ofstream out(corrupt,std::ios::binary);out<<"not png";}
    fails([&]{AssetLoader::loadTexture(corrupt);},"corrupt.png");
    for(auto type:{LCT_RGB,LCT_PALETTE}){
        auto path=dir/(type==LCT_RGB?"rgb.png":"indexed.png");fixture(path,type,8);
        fails([&]{AssetLoader::loadTexture(path);},path.filename().string());
    }
    auto sixteen=dir/"sixteen.png";fixture(sixteen,LCT_RGBA,16);fails([&]{AssetLoader::loadTexture(sixteen);},"sixteen.png");
    auto wrongSize=dir/"size.png";fixture(wrongSize,LCT_RGBA,8,31,32);fails([&]{AssetLoader::loadTexture(wrongSize);},"size.png");
    auto alpha=TextureGenerator::generate(0,0);alpha.rgba[3]=0;TextureGenerator::writePng(dir/"alpha.png",alpha);
    fails([&]{AssetLoader::loadTexture(dir/"alpha.png");},"alpha.png");
    TextureGenerator::exportAssets(dir);
    auto generated=AssetLoader::load(dir);
    require(generated.layers[0].rgba==committed.layers[0].rgba,"PNG round trip");
    // The manifest, rather than filesystem order, controls array layer placement.
    {std::ofstream manifest(dir/"manifest.txt");manifest<<"BLOXIDE_TEXTURES 1 32 32 20 125 1.2.5\n";
     for(int i=19;i>=0;--i)manifest<<i<<' '<<TextureGroups[i/4]<<' '<<i%4<<' '<<TextureGroups[i/4]<<'_'<<i%4<<".png\n";}
    require(AssetLoader::load(dir).layers[19].rgba==committed.layers[19].rgba,"manifest reorder");
    {std::ofstream manifest(dir/"manifest.txt");manifest<<"BLOXIDE_TEXTURES 1 32 32 20 125 1.2.5\n0 grass_top 0 grass_top_0.png\n0 grass_top 0 grass_top_0.png\n";}
    fails([&]{AssetLoader::load(dir);},"manifest.txt");
    std::filesystem::remove_all(dir); // Only the unique test fixture directory created above.
}
void meshing(){
    require(textureBaseLayer(BlockType::Grass,2)==0&&textureBaseLayer(BlockType::Grass,3)==8&&textureBaseLayer(BlockType::Grass,0)==4,"Grass face mapping");
    require(textureBaseLayer(BlockType::Dirt,0)==8&&textureBaseLayer(BlockType::Stone,0)==12&&textureBaseLayer(BlockType::Bedrock,0)==16,"material layers");
    auto data=std::make_shared<ChunkData>();data->blocks.fill(BlockType::Air);
    for(int x=0;x<8;++x)for(int z=0;z<4;++z)data->set(x,10,z,BlockType::Grass);
    auto mesh=ChunkMesher{}.build({},data,{},0);
    bool largeTop=false;
    for(std::size_t i=0;i<mesh->vertices.size();i+=4){
        unsigned face=(mesh->vertices[i].packed>>26)&7U,base=(mesh->vertices[i].packed>>19)&127U;
        require(base==textureBaseLayer(BlockType::Grass,face),"packed layer integer");
        if(face==2){
            int x0=16,z0=16,x1=0,z1=0;
            for(int j=0;j<4;++j){auto packed=mesh->vertices[i+j].packed;int x=packed&31,z=(packed>>14)&31;x0=std::min(x0,x);x1=std::max(x1,x);z0=std::min(z0,z);z1=std::max(z1,z);}
            largeTop|=x1-x0==8&&z1-z0==4;
        }
    }
    require(largeTop,"greedy quad retained across per-block variants");
    auto mixed=std::make_shared<ChunkData>();mixed->blocks.fill(BlockType::Air);
    mixed->set(0,10,0,BlockType::Grass);mixed->set(1,10,0,BlockType::Dirt);
    auto mixedMesh=ChunkMesher{}.build({},mixed,{},0);bool mergedBottom=false;
    for(std::size_t i=0;i<mixedMesh->vertices.size();i+=4){
        auto p=mixedMesh->vertices[i].packed;
        if(((p>>26)&7U)!=3)continue;
        int low=16,high=0;
        for(int j=0;j<4;++j){int x=mixedMesh->vertices[i+j].packed&31U;low=std::min(low,x);high=std::max(high,x);}
        mergedBottom|=high-low==2;
    }
    require(mergedBottom,"shared base-layer merge key");
    std::set<unsigned> variants;
    for(int x=-32;x<32;++x)variants.insert(textureVariant(x,10,-17,0,125));
    require(variants.size()==4,"coordinate hash uses every variant");
}
int main(){try{physics();textures();meshing();std::cout<<"v1.2.5 physics, assets, generator and meshing tests passed\n";}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
