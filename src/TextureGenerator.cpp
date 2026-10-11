#include <bloxide/Textures.hpp>
#include <lodepng.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
namespace bloxide {
namespace {
std::uint32_t hash(int x,int y,std::uint32_t seed){
    std::uint32_t h=std::uint32_t(x)*0x9e3779b9U^std::uint32_t(y)*0x85ebca6bU^seed;
    h^=h>>16;h*=0x7feb352dU;h^=h>>15;h*=0x846ca68bU;return h^(h>>16);
}
double unit(std::uint32_t x){return double(x)/4294967295.0;}
int wrap(int x,int n){return (x%n+n)%n;}
double smooth(double v){return v*v*(3-2*v);}
double fbm(double x,double y,std::uint32_t seed){
    double value=0,weight=0.5,total=0;
    for(int octave=0;octave<4;++octave){
        value+=weight*TextureGenerator::periodicNoise(x,y,2<<octave,seed+octave*1013U);
        total+=weight;weight*=0.5;
    }
    return value/total;
}
struct Cells {double f1=100,f2=100;std::uint32_t id=0;};
Cells cellular(double x,double y,int cells,std::uint32_t seed){
    x-=std::floor(x);y-=std::floor(y);
    double px=x*cells,py=y*cells;int cx=int(std::floor(px)),cy=int(std::floor(py));
    Cells result;
    for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){
        auto id=hash(wrap(cx+ox,cells),wrap(cy+oy,cells),seed);
        double dx=cx+ox+0.1+0.8*unit(id)-px;
        double dy=cy+oy+0.1+0.8*unit(hash(wrap(cx+ox,cells),wrap(cy+oy,cells),seed^0xab173U))-py;
        double d=std::hypot(dx,dy);
        if(d<result.f1){result.f2=result.f1;result.f1=d;result.id=id;}
        else result.f2=std::min(result.f2,d);
    }
    return result;
}
double blades(double x,double y,std::uint32_t seed){
    double value=0;
    for(int i=0;i<18;++i){
        auto h=hash(i,2,seed);double cx=unit(h)*31,cy=unit(hash(i,3,seed))*31;
        double dx=x*31-cx,dy=y*31-cy;
        dx-=std::round(dx/31)*31;dy-=std::round(dy/31)*31;
        double bx=(h&1)?1.3:-1.3,by=-2.0-(h%4);
        double t=std::clamp((dx*bx+dy*by)/(bx*bx+by*by),0.0,1.0);
        double d=std::hypot(dx-t*bx,dy-t*by);
        if(d<0.6)value+=0.18;
        else if(d<1.1)value-=0.04;
    }
    return std::clamp(value,-0.12,0.26);
}
double field(unsigned group,double x,double y,std::uint32_t seed){
    double wx=x+0.065*(fbm(x,y,seed+5)-0.5);
    double wy=y+0.065*(fbm(x,y,seed+17)-0.5);
    auto cell=cellular(wx,wy,group==3?4:group==4?3:7,seed+131);
    double fine=TextureGenerator::periodicNoise(wx,wy,16,seed+71);
    double value=0.51+0.46*(fbm(wx,wy,seed)-0.5)+0.10*(fine-0.5);
    double gap=cell.f2-cell.f1;
    if(group==0){
        value+=0.12*(gap-0.3)+blades(x,y,seed+491);
        if(cell.f1<0.14)value+=0.12;
    }else if(group==1||group==2){
        // Intentional gravel islands and fine soil grains.
        value+=0.11*(unit(cell.id)-0.5);
        if(cell.f1<0.18&&(cell.id%4==0))value+=0.24;
        if(cell.f1<0.10&&(cell.id%7==0))value-=0.18;
    }else if(group==3){
        value+=0.10*(unit(cell.id)-0.5);
        if(gap<0.065)value-=0.28;
        else if(gap<0.13)value+=0.10;
        if(cell.f1<0.16)value+=0.11;
    }else {
        if(gap<0.12)value-=0.27;
        else if(gap<0.19)value+=0.12;
        if(cell.f1<0.28)value+=0.16;
        if(fine>0.76)value-=0.12;
    }
    return std::clamp(value,0.0,1.0);
}
void put(TextureImage& image,unsigned x,unsigned y,const Pixel& color){
    auto i=(std::size_t(y)*image.width+x)*4;
    std::copy(color.begin(),color.end(),image.rgba.begin()+i);
}
void blit(TextureImage& target,const TextureImage& src,int dx,int dy,int scale){
    for(unsigned y=0;y<src.height;++y)for(unsigned x=0;x<src.width;++x){
        auto i=(std::size_t(y)*src.width+x)*4;Pixel p{src.rgba[i],src.rgba[i+1],src.rgba[i+2],255};
        for(int sy=0;sy<scale;++sy)for(int sx=0;sx<scale;++sx)put(target,dx+x*scale+sx,dy+y*scale+sy,p);
    }
}
}
double TextureGenerator::periodicNoise(double x,double y,int cells,std::uint32_t seed){
    x-=std::floor(x);y-=std::floor(y);
    double px=x*cells,py=y*cells;int ix=int(std::floor(px)),iy=int(std::floor(py));
    double fx=smooth(px-ix),fy=smooth(py-iy);
    auto get=[&](int a,int b){return unit(hash(wrap(a,cells),wrap(b,cells),seed));};
    double a=get(ix,iy)*(1-fx)+get(ix+1,iy)*fx;
    double b=get(ix,iy+1)*(1-fx)+get(ix+1,iy+1)*fx;
    return a*(1-fy)+b*fy;
}
Palette TextureGenerator::palette(unsigned group){
    switch(group){
    case 0:return {{29,58,28,255},{40,72,31,255},{51,88,35,255},{62,102,39,255},{75,116,44,255},{88,130,48,255},{105,145,56,255},{123,159,64,255},{142,174,75,255},{163,187,91,255}};
    case 1:return {{56,35,23,255},{75,48,28,255},{93,60,33,255},{113,75,40,255},{135,96,52,255},{162,123,72,255},{34,66,29,255},{48,83,33,255},{65,102,39,255},{83,122,45,255},{108,144,55,255},{137,166,70,255}};
    case 2:return {{53,33,22,255},{66,42,25,255},{80,50,29,255},{94,61,35,255},{108,73,41,255},{121,84,46,255},{135,98,55,255},{148,111,65,255},{164,128,80,255},{181,148,100,255}};
    case 3:return {{43,48,51,255},{57,63,66,255},{70,76,79,255},{83,89,91,255},{97,103,104,255},{111,117,117,255},{124,130,129,255},{139,145,141,255},{155,161,155,255},{172,177,168,255}};
    case 4:return {{17,20,24,255},{26,30,35,255},{34,39,44,255},{42,48,52,255},{51,56,60,255},{60,66,69,255},{70,76,78,255},{82,87,88,255},{96,100,100,255},{113,117,114,255}};
    default:throw std::invalid_argument("invalid texture group");
    }
}
TextureImage TextureGenerator::generate(unsigned group,unsigned variant,std::uint32_t seed){
    if(group>=5||variant>=4)throw std::invalid_argument("invalid texture group/variant");
    TextureImage image;auto colors=palette(group);
    constexpr int bayer[4][4]{{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    std::uint32_t shared=seed^((group+1)*0x9e3779b9U),specific=shared^((variant+1)*0x85ebca6bU);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        const double u=x/31.0,v=y/31.0;
        int distance=std::min(x,31-x);if(group!=1)distance=std::min(distance,std::min(y,31-y));
        double blend=smooth(std::clamp((distance-1)/5.0,0.0,1.0));
        double value=field(group,u,v,shared)*(1-blend)+field(group,u,v,specific)*blend;
        unsigned first=0,count=static_cast<unsigned>(colors.size());
        if(group==1){
            double band=6+5*periodicNoise(u,0,4,shared+991)*(1-blend)+5*periodicNoise(u,0,4,specific+991)*blend;
            if(y<band){
                first=6;count=6;
                value=0.48+0.35*(fbm(u,v,shared+771)*(1-blend)+fbm(u,v,specific+771)*blend-0.5);
                value+=blades(u,v,shared+771)*0.6;
                if(y<2)value+=0.13;
                if(y>band-2)value-=0.12;
            }else {count=6;if(y<band+2)value-=0.15;}
        }
        int bx=x==31?0:x,by=group!=1&&y==31?0:y;
        double dither=((bayer[by%4][bx%4]+0.5)/16.0-0.5)*0.7;
        int shade=static_cast<int>(std::floor(value*(count-1)+0.5+dither));
        shade=std::clamp(shade,0,static_cast<int>(count)-1);
        put(image,x,y,colors[first+shade]);
    }
    return image;
}
void TextureGenerator::writePng(const std::filesystem::path& file,const TextureImage& image){
    if(image.rgba.size()!=std::size_t(image.width)*image.height*4)throw std::runtime_error("invalid RGBA image buffer");
    lodepng::State state;state.encoder.auto_convert=0;
    state.info_raw.colortype=state.info_png.color.colortype=LCT_RGBA;
    state.info_raw.bitdepth=state.info_png.color.bitdepth=8;
    std::vector<unsigned char> bytes;
    auto error=lodepng::encode(bytes,image.rgba,image.width,image.height,state);
    if(!error)error=lodepng::save_file(bytes,file.string());
    if(error)throw std::runtime_error(file.string()+": "+lodepng_error_text(error));
}
void TextureGenerator::exportAssets(const std::filesystem::path& dir,std::uint32_t seed){
    std::filesystem::create_directories(dir);
    std::ofstream manifest(dir/"manifest.txt");
    if(!manifest)throw std::runtime_error("cannot create texture manifest");
    manifest<<"BLOXIDE_TEXTURES 1 32 32 20 "<<seed<<" 1.2.5\n";
    std::array<TextureImage,20> images;
    for(unsigned layer=0;layer<20;++layer){
        unsigned group=layer/4,variant=layer%4;
        std::string filename=std::string(TextureGroups[group])+"_"+std::to_string(variant)+".png";
        images[layer]=generate(group,variant,seed);writePng(dir/filename,images[layer]);
        manifest<<layer<<' '<<TextureGroups[group]<<' '<<variant<<' '<<filename<<'\n';
    }
    TextureImage sheet;sheet.width=4*132+4;sheet.height=5*132+4;sheet.rgba.assign(sheet.width*sheet.height*4,255);
    for(std::size_t i=0;i<sheet.rgba.size();i+=4){sheet.rgba[i]=25;sheet.rgba[i+1]=29;sheet.rgba[i+2]=34;}
    for(unsigned layer=0;layer<20;++layer)blit(sheet,images[layer],4+(layer%4)*132,4+(layer/4)*132,4);
    writePng(dir/"contact_sheet.png",sheet);
    TextureImage tiled;tiled.width=5*292+4;tiled.height=292;tiled.rgba.assign(tiled.width*tiled.height*4,255);
    for(std::size_t i=0;i<tiled.rgba.size();i+=4){tiled.rgba[i]=25;tiled.rgba[i+1]=29;tiled.rgba[i+2]=34;}
    for(unsigned group=0;group<5;++group)for(int y=0;y<3;++y)for(int x=0;x<3;++x)
        blit(tiled,images[group*4+(x+2*y)%4],4+group*292+x*96,2+y*96,3);
    writePng(dir/"tiled_preview.png",tiled);
}
}
