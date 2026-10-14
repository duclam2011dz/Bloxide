#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace bloxide {

enum class BlockType : unsigned char {
    Air = 0,
    Bedrock,
    Grass,
    Dirt,
    Stone,
    IronOre, GoldOre, DiamondOre, Water,
    OakLog, OakLeaves, OakPlanks,
    BirchLog, BirchLeaves, BirchPlanks,
    PineLog, PineLeaves, PinePlanks,
    Sand, Glass, Bricks, SlateRoof, RedFlower, YellowFlower, Cobblestone, Count
};
enum class RenderClass:unsigned char {Invisible,Opaque,Cutout,Translucent};
struct BlockState {
    std::uint16_t value=0;
    constexpr BlockState()=default;
    constexpr BlockState(BlockType type):value(static_cast<unsigned char>(type)){}
    constexpr BlockType type()const{return static_cast<BlockType>(value&255);}
    constexpr operator BlockType()const{return type();}
    explicit constexpr operator unsigned char()const{return static_cast<unsigned char>(value&255);}
    explicit constexpr operator unsigned()const{return value&255;}
    friend constexpr bool operator==(BlockState a,BlockState b){return a.value==b.value;}
    friend constexpr bool operator!=(BlockState a,BlockState b){return a.value!=b.value;}
    friend constexpr bool operator==(BlockState a,BlockType b){return a.type()==b;}
    friend constexpr bool operator!=(BlockState a,BlockType b){return a.type()!=b;}
    friend constexpr bool operator==(BlockType a,BlockState b){return b==a;}
    friend constexpr bool operator!=(BlockType a,BlockState b){return b!=a;}
    static constexpr BlockState water(unsigned level=0,bool falling=false){
        BlockState s(BlockType::Water);s.value|=std::uint16_t(((level&7)|(falling?8:0))<<8);return s;
    }
    constexpr unsigned waterLevel()const{return (value>>8)&7;}
    constexpr bool falling()const{return (value&(8<<8))!=0;}
    constexpr bool source()const{return type()==BlockType::Water&&waterLevel()==0&&!falling();}
    float waterHeight()const{return falling()?1.0f:float(8-(waterLevel()?waterLevel():1))/8.0f;}
};
static_assert(sizeof(BlockState)==2,"BlockState wire width");
RenderClass renderClass(BlockType) noexcept;
bool isOpaque(BlockType) noexcept;
bool isTargetable(BlockType) noexcept;
bool isReplaceable(BlockType) noexcept;
bool isFlower(BlockType) noexcept;
std::string_view blockName(BlockType) noexcept;

struct Color {
    float r;
    float g;
    float b;
};

[[nodiscard]] bool isSolid(BlockType block) noexcept;
[[nodiscard]] bool isBreakable(BlockType block) noexcept;
[[nodiscard]] Color blockColor(BlockType block) noexcept;

} // namespace bloxide
