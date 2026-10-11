#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/Coordinates.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bloxide {
ChunkGenerator::ChunkGenerator(const WorldSettings& s):settings_(s),
    continental_(SeedDerivation::derive(s.seed,SeedDomain::Continental)),ridges_(SeedDerivation::derive(s.seed,SeedDomain::Ridges)),
    hills_(SeedDerivation::derive(s.seed,SeedDomain::Hills)),turbulence_(SeedDerivation::derive(s.seed,SeedDomain::Turbulence)),
    warpX_(SeedDerivation::derive(s.seed,SeedDomain::WarpX)),warpZ_(SeedDerivation::derive(s.seed,SeedDomain::WarpZ)),
    caves_(SeedDerivation::derive(s.seed,SeedDomain::Caves)),caveWarpX_(SeedDerivation::derive(s.seed,SeedDomain::CaveWarpX)),
    caveWarpY_(SeedDerivation::derive(s.seed,SeedDomain::CaveWarpY)),caveWarpZ_(SeedDerivation::derive(s.seed,SeedDomain::CaveWarpZ)){settings_.validate();}
int ChunkGenerator::surfaceHeight(std::int64_t x,std::int64_t z){static const ChunkGenerator generator;return generator.heightAt(x,z);}
int ChunkGenerator::heightAt(std::int64_t x,std::int64_t z)const {
    if(x<-WorldLimit-256||x>WorldLimit+256||z<-WorldLimit-256||z>WorldLimit+256)throw std::out_of_range("terrain coordinate out of bounds");
    auto warped=FractalNoise::warp2D(warpX_,warpZ_,settings_.warp2D,double(x),double(z),settings_.warpStrength);
    double wx=warped[0],wz=warped[1];
    auto continental=FractalNoise::sample2D(continental_,settings_.continental,wx,wz);
    auto ridge=FractalNoise::sample2D(ridges_,settings_.ridges,wx,wz,FractalMode::Ridged);
    auto hills=FractalNoise::sample2D(hills_,settings_.hills,wx,wz,FractalMode::Billow);
    auto erosion=FractalNoise::sample2D(turbulence_,settings_.turbulence,wx,wz,FractalMode::Turbulence);
    return static_cast<int>(std::floor(std::clamp(64+72*continental+58*ridge+12*hills-10*erosion,24.0,192.0)));
}
double ChunkGenerator::caveNode(std::int64_t x,int y,std::int64_t z)const {
    auto warped=FractalNoise::warp3D(caveWarpX_,caveWarpY_,caveWarpZ_,settings_.caveWarp,double(x),y,double(z),settings_.caveWarpStrength,4);
    return FractalNoise::sample3D(caves_,settings_.caveDensity,warped[0],warped[1],warped[2],FractalMode::Fbm,4);
}
std::shared_ptr<ChunkData> ChunkGenerator::generate(ChunkCoord c,std::uint64_t revision)const {
    if(c.x<-WorldLimit/16-16||c.x>WorldLimit/16+15||c.z<-WorldLimit/16-16||c.z>WorldLimit/16+15)throw std::out_of_range("chunk generation bounds");
    std::array<int,256> heights{};int highest=0;
    const auto baseX=c.x*16,baseZ=c.z*16;
    for(int z=0;z<16;++z)for(int x=0;x<16;++x){int h=heightAt(baseX+x,baseZ+z);heights[z*16+x]=h;highest=std::max(highest,h);}
    const int levels=highest/4+2;
    std::vector<double> density(std::size_t(5*5*levels));
    auto index=[](int x,int y,int z){return (y*5+z)*5+x;};
    for(int y=1;y<levels;++y)for(int z=0;z<5;++z)for(int x=0;x<5;++x)density[index(x,y,z)]=caveNode(baseX+x*4,y*4,baseZ+z*4);
    auto data=std::make_shared<ChunkData>();data->blocks.fill(BlockType::Air);
    for(int z=0;z<16;++z)for(int x=0;x<16;++x){
        int h=heights[z*16+x];data->blocks[data->index(x,0,z)]=BlockType::Bedrock;
        for(int y=1;y<=h;++y){
            bool carve=false;
            if(y>=4){
                int gx=x/4,gy=y/4,gz=z/4;double fx=(x%4)/4.0,fy=(y%4)/4.0,fz=(z%4)/4.0;
                double v=0;
                for(int dy=0;dy<2;++dy)for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx)
                    v+=density[index(gx+dx,gy+dy,gz+dz)]*(dx?fx:1-fx)*(dy?fy:1-fy)*(dz?fz:1-fz);
                carve=v>settings_.caveThreshold;
            }
            if(!carve)data->blocks[data->index(x,y,z)]=y==h?BlockType::Grass:y>=h-3?BlockType::Dirt:BlockType::Stone;
        }
    }
    data->version=revision;return data;
}
}
