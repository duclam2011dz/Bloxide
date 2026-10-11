#include <bloxide/Noise.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bloxide {
std::uint64_t SplitMix64::mix(std::uint64_t x){x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
std::uint64_t SplitMix64::next(){state_+=0x9e3779b97f4a7c15ULL;return mix(state_);}
std::uint64_t SplitMix64::bounded(std::uint64_t n){if(!n)throw std::invalid_argument("zero RNG bound");auto threshold=(0-n)%n;for(;;){auto v=next();if(v>=threshold)return v%n;}}
std::uint64_t SeedDerivation::derive(std::uint64_t root,SeedDomain d){return SplitMix64::mix(root^SplitMix64::mix(static_cast<std::uint64_t>(d)+0xd1b54a32d192ed03ULL));}
std::uint64_t SeedDerivation::coordinateHash(std::uint64_t seed,std::int64_t x,std::int64_t y,std::int64_t z){
    auto a=SplitMix64::mix(static_cast<std::uint64_t>(x)+0x9e3779b97f4a7c15ULL);
    auto b=SplitMix64::mix(static_cast<std::uint64_t>(y)+0xbf58476d1ce4e5b9ULL);
    auto c=SplitMix64::mix(static_cast<std::uint64_t>(z)+0x94d049bb133111ebULL);
    return SplitMix64::mix(seed^a^((b<<21)|(b>>43))^((c<<42)|(c>>22)));
}
namespace {
void shuffle(std::array<unsigned char,256>& p,std::uint64_t seed){
    for(int i=0;i<256;++i)p[i]=static_cast<unsigned char>(i);
    SplitMix64 rng(seed);
    for(int i=255;i>0;--i)std::swap(p[i],p[rng.bounded(i+1)]);
}
void finite(double x){if(!std::isfinite(x)||std::abs(x)>17592186044416.0)throw std::domain_error("simplex coordinate outside finite +/-2^44 domain");}
std::int64_t floorCell(double v){return static_cast<std::int64_t>(std::floor(v));}
constexpr double S=0.7071067811865475244;
constexpr double g2[8][2]{{1,0},{-1,0},{0,1},{0,-1},{S,S},{-S,S},{S,-S},{-S,-S}};
constexpr double g3[12][3]{{S,S,0},{-S,S,0},{S,-S,0},{-S,-S,0},{S,0,S},{-S,0,S},{S,0,-S},{-S,0,-S},{0,S,S},{0,-S,S},{0,S,-S},{0,-S,-S}};
double contribution2(double x,double y,unsigned gradient){
    double t=0.5-x*x-y*y;if(t<=0)return 0;t*=t;
    return t*t*(g2[gradient][0]*x+g2[gradient][1]*y);
}
double contribution3(double x,double y,double z,unsigned gradient){
    double t=0.6-x*x-y*y-z*z;if(t<=0)return 0;t*=t;
    return t*t*(g3[gradient][0]*x+g3[gradient][1]*y+g3[gradient][2]*z);
}
template<class Sample> double fractal(const FractalSettings& s,FractalMode mode,double footprint,Sample sample){
    if(!std::isfinite(footprint)||footprint<=0)throw std::domain_error("invalid sample footprint");
    double f=s.frequency,a=1,total=0,value=0,feedback=1;
    for(int octave=0;octave<s.octaves;++octave){
        if(f>0.5/footprint||a==0)break;
        double n=sample(f),v=n;
        if(mode==FractalMode::Billow)v=2*std::abs(n)-1;
        if(mode==FractalMode::Turbulence)v=std::abs(n);
        if(mode==FractalMode::Ridged){v=1-std::abs(n);v=v*v*feedback;feedback=std::clamp(v*2,0.0,1.0);}
        value+=a*v;total+=a;f*=s.lacunarity;a*=s.gain;
    }
    return total>0?value/total:0;
}
}
SimplexNoise2D::SimplexNoise2D(std::uint64_t seed):seed_(seed){shuffle(permutation_,seed);}
SimplexNoise3D::SimplexNoise3D(std::uint64_t seed):seed_(seed){shuffle(permutation_,seed);}
double SimplexNoise2D::sample(double x,double y)const {
    finite(x);finite(y);
    constexpr double F=0.36602540378443864676,G=0.21132486540518711775;
    double skew=(x+y)*F;auto i=floorCell(x+skew),j=floorCell(y+skew);
    double unskew=double(i+j)*G,x0=x-(double(i)-unskew),y0=y-(double(j)-unskew);
    int i1=x0>y0?1:0,j1=1-i1;
    auto grad=[&](std::int64_t a,std::int64_t b){return permutation_[SeedDerivation::coordinateHash(seed_,a,b,0)&255U]%8U;};
    double n=contribution2(x0,y0,grad(i,j))
            +contribution2(x0-i1+G,y0-j1+G,grad(i+i1,j+j1))
            +contribution2(x0-1+2*G,y0-1+2*G,grad(i+1,j+1));
    return std::clamp(n*99.0,-1.0,1.0);
}
double SimplexNoise3D::sample(double x,double y,double z)const {
    finite(x);finite(y);finite(z);
    constexpr double F=1.0/3,G=1.0/6;
    double skew=(x+y+z)*F;auto i=floorCell(x+skew),j=floorCell(y+skew),k=floorCell(z+skew);
    double t=double(i+j+k)*G,x0=x-(double(i)-t),y0=y-(double(j)-t),z0=z-(double(k)-t);
    int i1,j1,k1,i2,j2,k2;
    if(x0>=y0){
        if(y0>=z0){i1=1;j1=0;k1=0;i2=1;j2=1;k2=0;}
        else if(x0>=z0){i1=1;j1=0;k1=0;i2=1;j2=0;k2=1;}
        else{i1=0;j1=0;k1=1;i2=1;j2=0;k2=1;}
    }else{
        if(y0<z0){i1=0;j1=0;k1=1;i2=0;j2=1;k2=1;}
        else if(x0<z0){i1=0;j1=1;k1=0;i2=0;j2=1;k2=1;}
        else{i1=0;j1=1;k1=0;i2=1;j2=1;k2=0;}
    }
    auto grad=[&](std::int64_t a,std::int64_t b,std::int64_t c){return permutation_[SeedDerivation::coordinateHash(seed_,a,b,c)&255U]%12U;};
    double n=contribution3(x0,y0,z0,grad(i,j,k))
            +contribution3(x0-i1+G,y0-j1+G,z0-k1+G,grad(i+i1,j+j1,k+k1))
            +contribution3(x0-i2+2*G,y0-j2+2*G,z0-k2+2*G,grad(i+i2,j+j2,k+k2))
            +contribution3(x0-1+3*G,y0-1+3*G,z0-1+3*G,grad(i+1,j+1,k+1));
    return std::clamp(n*45.0,-1.0,1.0);
}
void FractalSettings::validate()const{
    if(octaves<1||octaves>12||!std::isfinite(frequency)||frequency<=0||frequency>0.5||
       !std::isfinite(lacunarity)||lacunarity<1||lacunarity>4||!std::isfinite(gain)||gain<0||gain>1)throw std::invalid_argument("invalid fractal config");
}
double FractalNoise::sample2D(const SimplexNoise2D& n,const FractalSettings&s,double x,double z,FractalMode mode,double footprint){
    finite(x);finite(z);return fractal(s,mode,footprint,[&](double f){return n.sample(x*f,z*f);});
}
double FractalNoise::sample3D(const SimplexNoise3D& n,const FractalSettings&s,double x,double y,double z,FractalMode mode,double footprint){
    finite(x);finite(y);finite(z);return fractal(s,mode,footprint,[&](double f){return n.sample(x*f,y*f,z*f);});
}
std::array<double,2> FractalNoise::warp2D(const SimplexNoise2D& nx,const SimplexNoise2D& nz,const FractalSettings& s,double x,double z,double strength,double footprint){
    if(!std::isfinite(strength)||strength<0)throw std::domain_error("invalid warp strength");
    return {x+strength*sample2D(nx,s,x,z,FractalMode::Fbm,footprint),z+strength*sample2D(nz,s,x,z,FractalMode::Fbm,footprint)};
}
std::array<double,3> FractalNoise::warp3D(const SimplexNoise3D& nx,const SimplexNoise3D& ny,const SimplexNoise3D& nz,const FractalSettings& s,double x,double y,double z,double strength,double footprint){
    if(!std::isfinite(strength)||strength<0)throw std::domain_error("invalid warp strength");
    return {x+strength*sample3D(nx,s,x,y,z,FractalMode::Fbm,footprint),y+strength*sample3D(ny,s,x,y,z,FractalMode::Fbm,footprint),z+strength*sample3D(nz,s,x,y,z,FractalMode::Fbm,footprint)};
}
}
