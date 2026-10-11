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
void WorldSettings::validate()const {
    if(generatorVersion!="simplex-1")throw std::invalid_argument("unsupported generator version '"+generatorVersion+"'; expected simplex-1");
    if(preset!="mountains-v1")throw std::invalid_argument("unsupported terrain preset '"+preset+"'");
    continental.validate();ridges.validate();hills.validate();turbulence.validate();warp2D.validate();caveWarp.validate();caveDensity.validate();
    if(!std::isfinite(warpStrength)||warpStrength<0||warpStrength>256||
       !std::isfinite(caveWarpStrength)||caveWarpStrength<0||caveWarpStrength>64||
       !std::isfinite(caveThreshold)||caveThreshold<=0||caveThreshold>=1)throw std::invalid_argument("invalid world noise config");
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
        out<<"format_version 1\nseed "<<seed<<"\ngenerator_version "<<generatorVersion<<"\npreset "<<preset<<'\n';
        WorldSettings copy=*this;auto configs=fields(copy);
        for(int i=0;i<7;++i){
            auto& f=*configs[i];out<<names[i]<<".octaves "<<f.octaves<<'\n'<<names[i]<<".frequency "<<f.frequency<<'\n'
            <<names[i]<<".lacunarity "<<f.lacunarity<<'\n'<<names[i]<<".gain "<<f.gain<<'\n';
        }
        out<<"warpStrength "<<warpStrength<<"\ncaveWarpStrength "<<caveWarpStrength<<"\ncaveThreshold "<<caveThreshold<<'\n';
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
    if(get("format_version")!="1")throw std::runtime_error("unsupported metadata format");
    settings.seed=parseSeed(get("seed"));settings.generatorVersion=get("generator_version");settings.preset=get("preset");
    auto configs=fields(settings);
    for(int i=0;i<7;++i){
        auto prefix=std::string(names[i]);double count=number(prefix+".octaves");
        if(count<1||count>12||std::floor(count)!=count)throw std::runtime_error("invalid octave count");
        configs[i]->octaves=static_cast<int>(count);configs[i]->frequency=number(prefix+".frequency");
        configs[i]->lacunarity=number(prefix+".lacunarity");configs[i]->gain=number(prefix+".gain");
    }
    settings.warpStrength=number("warpStrength");settings.caveWarpStrength=number("caveWarpStrength");settings.caveThreshold=number("caveThreshold");
    if(!data.empty())throw std::runtime_error("unknown metadata key: "+data.begin()->first);
    try{settings.validate();}catch(const std::exception&e){throw std::runtime_error(file.string()+": "+e.what());}
    return settings;
}
WorldSettings WorldSettings::open(const std::filesystem::path& dir,std::optional<std::uint64_t> seed){
    auto path=dir/"world.meta";
    if(std::filesystem::exists(path)){
        auto existing=load(path);
        if(seed&&*seed!=existing.seed)throw std::runtime_error("CLI seed conflicts with existing world metadata");
        return existing;
    }
    WorldSettings settings;
    if(seed)settings.seed=*seed;
    else {std::random_device random;settings.seed=(std::uint64_t(random())<<32)^random();}
    settings.save(path);return settings;
}
}
