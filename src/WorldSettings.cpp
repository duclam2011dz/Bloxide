#include <bloxide/WorldSettings.hpp>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace bloxide {
namespace {
const char* names[]{"continental","ridges","hills","turbulence","warp2D","caveWarp","caveDensity"};
std::array<FractalSettings*,7> fields(WorldSettings& s){return {&s.continental,&s.ridges,&s.hills,&s.turbulence,&s.warp2D,&s.caveWarp,&s.caveDensity};}
}
void DensitySettings::validate()const{
    terrain.validate();warp.validate();rockMask.validate();climate.validate();aquifer.validate();
    for(double v:{amplitude,warpStrength,band,baseScale,aquiferThreshold,caveDistributionMinimum,villageProbability})
        if(!std::isfinite(v))throw std::invalid_argument("nonfinite density config");
    if(amplitude<0||amplitude>2||warpStrength<0||warpStrength>64||band<8||band>96||baseScale<4||baseScale>64||
        ceiling<64||ceiling>248||seaLevel<4||seaLevel>192||waterTableBase<4||waterTableBase>192||waterTableAmplitude<0||waterTableAmplitude>48||
        aquiferThreshold<0||aquiferThreshold>1||caveDistributionMinimum<0||caveDistributionMinimum>1||
        oreCell!=16||treeCell<4||treeCell>32||villageCell<256||villageCell>4096||villageJitter<0||villageJitter>villageCell/4||
        villageProbability<0||villageProbability>1||villageMaxSlope<0||villageMaxSlope>12||villageMaxFoundation<1||villageMaxFoundation>24)
        throw std::invalid_argument("invalid density/feature config");
    for(auto& o:ore)if(o.minY<4||o.maxY>192||o.minY>o.maxY||!std::isfinite(o.probability)||o.probability<0||o.probability>1||
        o.minRadius<1||o.maxRadius>4||o.minRadius>o.maxRadius)throw std::invalid_argument("invalid ore config");
}
WorldSettings WorldSettings::densityWorld(std::string preset){
    auto s=terrainFields(std::move(preset));s.generatorVersion="density-world-1";
    if(s.preset=="rolling-hills")s.density.amplitude=0.35;
    if(s.preset=="flat")s.density.amplitude=0;
    s.validate();return s;
}
WorldSettings WorldSettings::terrainFields(std::string preset){
    WorldSettings s;s.generatorVersion="terrain-fields-1";s.preset=std::move(preset);
    s.continental={5,1.0/2048,2,0.5};
    if(s.preset=="flat")s.caves=false;
    if(s.preset=="rolling-hills"){s.ridgeAmplitude=8;s.valleyAmplitude=6;s.plateauBlend=0;s.minHeight=32;s.maxHeight=112;}
    s.validate();return s;
}
void WorldSettings::validate()const {
    if(generatorVersion!="simplex-1"&&generatorVersion!="terrain-fields-1"&&generatorVersion!="density-world-1")throw std::invalid_argument("unsupported generator version '"+generatorVersion+"'");
    if((generatorVersion=="simplex-1"&&preset!="mountains-v1")||
       (generatorVersion!="simplex-1"&&preset!="flat"&&preset!="rolling-hills"&&preset!="mountainous"))throw std::invalid_argument("unsupported terrain preset '"+preset+"'");
    density.validate();
    continental.validate();ridges.validate();hills.validate();turbulence.validate();warp2D.validate();caveWarp.validate();caveDensity.validate();
    if(!std::isfinite(warpStrength)||warpStrength<0||warpStrength>256||
       !std::isfinite(caveWarpStrength)||caveWarpStrength<0||caveWarpStrength>64||
       !std::isfinite(caveThreshold)||caveThreshold<=0||caveThreshold>=1)throw std::invalid_argument("invalid world noise config");
    roughness.validate();mountainMask.validate();valley.validate();plateau.validate();
    for(double v:{maskLow,maskHigh,hillsAmplitude,ridgeAmplitude,valleyAmplitude,plateauBlend,plateauHeight,minHeight,maxHeight})
        if(!std::isfinite(v))throw std::invalid_argument("nonfinite terrain config");
    if(maskLow<0||maskHigh>1||maskLow>=maskHigh||hillsAmplitude<0||hillsAmplitude>256||ridgeAmplitude<0||ridgeAmplitude>256||
       valleyAmplitude<0||valleyAmplitude>256||plateauBlend<0||plateauBlend>1||plateauHeight<1||plateauHeight>254||
       minHeight<4||maxHeight>254||minHeight>maxHeight)throw std::invalid_argument("invalid terrain shaping config");
    for(std::size_t i=0;i<5;++i)if(!std::isfinite(splineX[i])||!std::isfinite(splineY[i])||splineY[i]<1||splineY[i]>254||
        (i&&(splineX[i]<=splineX[i-1]||splineY[i]<splineY[i-1])))throw std::invalid_argument("invalid monotone spline");
    if(splineX.front()!=-1||splineX.back()!=1)throw std::invalid_argument("spline must span [-1,1]");
}
std::uint64_t WorldSettings::parseSeed(const std::string& text){
    std::uint64_t seed=0;
    auto result=std::from_chars(text.data(),text.data()+text.size(),seed);
    if(text.empty()||result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::invalid_argument("seed must be uint64 decimal");
    return seed;
}
void WorldSettings::save(const std::filesystem::path& file)const{
    validate();std::filesystem::create_directories(file.parent_path());
    auto temporary=file;temporary+= ".tmp";
    if(std::filesystem::exists(temporary))throw std::runtime_error("metadata temporary file already exists: "+temporary.string());
    try{
        std::ofstream out(temporary,std::ios::binary);out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);
        out<<"format_version "<<(generatorVersion=="simplex-1"?1:generatorVersion=="terrain-fields-1"?2:3)<<"\nseed "<<seed<<"\ngenerator_version "<<generatorVersion<<"\npreset "<<preset<<'\n';
        WorldSettings copy=*this;auto configs=fields(copy);
        for(int i=0;i<7;++i){
            auto& f=*configs[i];out<<names[i]<<".octaves "<<f.octaves<<'\n'<<names[i]<<".frequency "<<f.frequency<<'\n'
            <<names[i]<<".lacunarity "<<f.lacunarity<<'\n'<<names[i]<<".gain "<<f.gain<<'\n';
        }
        out<<"warpStrength "<<warpStrength<<"\ncaveWarpStrength "<<caveWarpStrength<<"\ncaveThreshold "<<caveThreshold<<'\n';
        if(generatorVersion!="simplex-1"){
            out<<"caves "<<(caves?1:0)<<'\n';
            const char* extra[]{"roughness","mountainMask","valley","plateau"};
            const FractalSettings* fs[]{&roughness,&mountainMask,&valley,&plateau};
            for(int i=0;i<4;++i)out<<extra[i]<<".octaves "<<fs[i]->octaves<<'\n'<<extra[i]<<".frequency "<<fs[i]->frequency<<'\n'
                <<extra[i]<<".lacunarity "<<fs[i]->lacunarity<<'\n'<<extra[i]<<".gain "<<fs[i]->gain<<'\n';
            for(int i=0;i<5;++i)out<<"spline.x"<<i<<' '<<splineX[i]<<'\n'<<"spline.y"<<i<<' '<<splineY[i]<<'\n';
            const char* keys[]{"maskLow","maskHigh","hillsAmplitude","ridgeAmplitude","valleyAmplitude","plateauBlend","plateauHeight","minHeight","maxHeight"};
            double values[]{maskLow,maskHigh,hillsAmplitude,ridgeAmplitude,valleyAmplitude,plateauBlend,plateauHeight,minHeight,maxHeight};
            for(int i=0;i<9;++i)out<<keys[i]<<' '<<values[i]<<'\n';
        }
        if(generatorVersion=="density-world-1"){
            const char* names[]{"terrain","warp","rockMask","climate","aquifer"};
            const FractalSettings* fs[]{&density.terrain,&density.warp,&density.rockMask,&density.climate,&density.aquifer};
            for(int i=0;i<5;++i)out<<"density."<<names[i]<<".octaves "<<fs[i]->octaves<<'\n'<<"density."<<names[i]<<".frequency "<<fs[i]->frequency<<'\n'
                <<"density."<<names[i]<<".lacunarity "<<fs[i]->lacunarity<<'\n'<<"density."<<names[i]<<".gain "<<fs[i]->gain<<'\n';
            const char* keys[]{"amplitude","warpStrength","band","baseScale","ceiling","seaLevel","waterTableBase","waterTableAmplitude","aquiferThreshold",
                "caveDistributionMinimum","water","ores","trees","villages","flowers","oreCell","treeCell","villageCell","villageJitter","villageProbability","villageMaxSlope","villageMaxFoundation"};
            double values[]{density.amplitude,density.warpStrength,density.band,density.baseScale,double(density.ceiling),double(density.seaLevel),
                double(density.waterTableBase),double(density.waterTableAmplitude),density.aquiferThreshold,density.caveDistributionMinimum,
                double(density.water),double(density.ores),double(density.trees),double(density.villages),double(density.flowers),
                double(density.oreCell),double(density.treeCell),double(density.villageCell),double(density.villageJitter),density.villageProbability,
                double(density.villageMaxSlope),double(density.villageMaxFoundation)};
            for(int i=0;i<22;++i)out<<"density."<<keys[i]<<' '<<values[i]<<'\n';
            for(int i=0;i<3;++i)out<<"ore."<<i<<".minY "<<density.ore[i].minY<<'\n'<<"ore."<<i<<".maxY "<<density.ore[i].maxY<<'\n'
                <<"ore."<<i<<".probability "<<density.ore[i].probability<<'\n'<<"ore."<<i<<".minRadius "<<density.ore[i].minRadius<<'\n'<<"ore."<<i<<".maxRadius "<<density.ore[i].maxRadius<<'\n';
        }
        out.flush();if(!out)throw std::runtime_error("metadata write failed");out.close();
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("atomic metadata replacement failed");
#else
        std::filesystem::rename(temporary,file);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
WorldSettings WorldSettings::load(const std::filesystem::path& file){
    if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>65536)throw std::runtime_error("invalid metadata file: "+file.string());
    std::ifstream in(file);in.imbue(std::locale::classic());
    std::map<std::string,std::string> data;std::string key,value;
    while(in>>key){if(!(in>>value)||!data.emplace(key,value).second)throw std::runtime_error("duplicate/truncated metadata: "+file.string());}
    auto get=[&](const std::string& name){auto it=data.find(name);if(it==data.end())throw std::runtime_error("metadata key missing: "+name);auto v=it->second;data.erase(it);return v;};
    auto number=[&](const std::string& name){std::istringstream s(get(name));s.imbue(std::locale::classic());double v;char tail;if(!(s>>v)||(s>>tail)||!std::isfinite(v))throw std::runtime_error("metadata number invalid: "+name);return v;};
    WorldSettings settings;
    auto format=get("format_version");if(format!="1"&&format!="2"&&format!="3")throw std::runtime_error("unsupported metadata format");
    settings.seed=parseSeed(get("seed"));settings.generatorVersion=get("generator_version");settings.preset=get("preset");
    auto configs=fields(settings);
    for(int i=0;i<7;++i){
        auto prefix=std::string(names[i]);double count=number(prefix+".octaves");
        if(count<1||count>12||std::floor(count)!=count)throw std::runtime_error("invalid octave count");
        configs[i]->octaves=static_cast<int>(count);configs[i]->frequency=number(prefix+".frequency");
        configs[i]->lacunarity=number(prefix+".lacunarity");configs[i]->gain=number(prefix+".gain");
    }
    settings.warpStrength=number("warpStrength");settings.caveWarpStrength=number("caveWarpStrength");settings.caveThreshold=number("caveThreshold");
    if(format=="2"||format=="3"){
        if(settings.generatorVersion!=(format=="2"?"terrain-fields-1":"density-world-1"))throw std::runtime_error("metadata version/generator mismatch");
        auto cave=get("caves");if(cave!="0"&&cave!="1")throw std::runtime_error("invalid caves flag");settings.caves=cave=="1";
        const char* extra[]{"roughness","mountainMask","valley","plateau"};
        FractalSettings* fs[]{&settings.roughness,&settings.mountainMask,&settings.valley,&settings.plateau};
        for(int i=0;i<4;++i){auto p=std::string(extra[i]);double n=number(p+".octaves");
            if(n<1||n>12||std::floor(n)!=n)throw std::runtime_error("invalid octave count");
            *fs[i]={int(n),number(p+".frequency"),number(p+".lacunarity"),number(p+".gain")};
        }
        for(int i=0;i<5;++i){settings.splineX[i]=number("spline.x"+std::to_string(i));settings.splineY[i]=number("spline.y"+std::to_string(i));}
        settings.maskLow=number("maskLow");settings.maskHigh=number("maskHigh");settings.hillsAmplitude=number("hillsAmplitude");
        settings.ridgeAmplitude=number("ridgeAmplitude");settings.valleyAmplitude=number("valleyAmplitude");settings.plateauBlend=number("plateauBlend");
        settings.plateauHeight=number("plateauHeight");settings.minHeight=number("minHeight");settings.maxHeight=number("maxHeight");
    }else if(settings.generatorVersion!="simplex-1")throw std::runtime_error("metadata version/generator mismatch");
    if(format=="3"){
        const char* names[]{"terrain","warp","rockMask","climate","aquifer"};
        FractalSettings* fs[]{&settings.density.terrain,&settings.density.warp,&settings.density.rockMask,&settings.density.climate,&settings.density.aquifer};
        auto integer=[&](const std::string& key){double n=number(key);if(n<-4096||n>4096||std::floor(n)!=n)throw std::runtime_error("invalid integer: "+key);return int(n);};
        for(int i=0;i<5;++i){auto p=std::string("density.")+names[i];*fs[i]={integer(p+".octaves"),number(p+".frequency"),number(p+".lacunarity"),number(p+".gain")};}
        auto& d=settings.density;d.amplitude=number("density.amplitude");d.warpStrength=number("density.warpStrength");d.band=number("density.band");d.baseScale=number("density.baseScale");
        d.ceiling=integer("density.ceiling");d.seaLevel=integer("density.seaLevel");d.waterTableBase=integer("density.waterTableBase");d.waterTableAmplitude=integer("density.waterTableAmplitude");
        d.aquiferThreshold=number("density.aquiferThreshold");d.caveDistributionMinimum=number("density.caveDistributionMinimum");
        auto flag=[&](const char* key){auto n=integer(std::string("density.")+key);if(n!=0&&n!=1)throw std::runtime_error("invalid feature flag");return n==1;};
        d.water=flag("water");d.ores=flag("ores");d.trees=flag("trees");d.villages=flag("villages");d.flowers=flag("flowers");
        d.oreCell=integer("density.oreCell");d.treeCell=integer("density.treeCell");d.villageCell=integer("density.villageCell");d.villageJitter=integer("density.villageJitter");
        d.villageProbability=number("density.villageProbability");d.villageMaxSlope=integer("density.villageMaxSlope");d.villageMaxFoundation=integer("density.villageMaxFoundation");
        for(int i=0;i<3;++i){auto p="ore."+std::to_string(i);d.ore[i]={integer(p+".minY"),integer(p+".maxY"),number(p+".probability"),integer(p+".minRadius"),integer(p+".maxRadius")};}
    }
    if(!data.empty())throw std::runtime_error("unknown metadata key: "+data.begin()->first);
    try{settings.validate();}catch(const std::exception&e){throw std::runtime_error(file.string()+": "+e.what());}
    return settings;
}
WorldSettings WorldSettings::open(const std::filesystem::path& dir,std::optional<std::uint64_t> seed,std::optional<std::string> preset,std::optional<bool> caves){
    auto path=dir/"world.meta";
    if(std::filesystem::exists(path)){
        auto existing=load(path);
        if(seed&&*seed!=existing.seed)throw std::runtime_error("CLI seed conflicts with existing world metadata");
        if(preset&&*preset!=existing.preset)throw std::runtime_error("CLI preset conflicts with existing world metadata");
        if(caves&&*caves!=existing.caves)throw std::runtime_error("CLI caves conflicts with existing world metadata");
        return existing;
    }
    WorldSettings settings=densityWorld(preset.value_or("mountainous"));
    if(caves)settings.caves=*caves;
    if(seed)settings.seed=*seed;
    else {std::random_device random;settings.seed=(std::uint64_t(random())<<32)^random();}
    settings.save(path);return settings;
}
}
