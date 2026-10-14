#include "bloxide/Block.hpp"

namespace bloxide {

bool isSolid(BlockType block) noexcept {
    return block!=BlockType::Air&&block!=BlockType::Water&&!isFlower(block)&&block<BlockType::Count;
}

bool isBreakable(BlockType block) noexcept {
    return isTargetable(block)&&block!=BlockType::Bedrock;
}
bool isFlower(BlockType b)noexcept{return b==BlockType::RedFlower||b==BlockType::YellowFlower;}
bool isTargetable(BlockType b)noexcept{return b!=BlockType::Air&&b!=BlockType::Water&&b<BlockType::Count;}
bool isReplaceable(BlockType b)noexcept{return b==BlockType::Air||b==BlockType::Water||isFlower(b);}
RenderClass renderClass(BlockType b)noexcept{
    if(b==BlockType::Air||b>=BlockType::Count)return RenderClass::Invisible;
    if(b==BlockType::Water||b==BlockType::Glass)return RenderClass::Translucent;
    if(b==BlockType::OakLeaves||b==BlockType::BirchLeaves||b==BlockType::PineLeaves||isFlower(b))return RenderClass::Cutout;
    return RenderClass::Opaque;
}
bool isOpaque(BlockType b)noexcept{return renderClass(b)==RenderClass::Opaque;}
std::string_view blockName(BlockType b)noexcept{
    constexpr std::array<std::string_view,25> names{"Air","Bedrock","Grass","Dirt","Stone","Iron Ore","Gold Ore","Diamond Ore","Water",
        "Oak Log","Oak Leaves","Oak Planks","Birch Log","Birch Leaves","Birch Planks","Pine Log","Pine Leaves","Pine Planks",
        "Sand","Glass","Bricks","Slate Roof","Red Flower","Yellow Flower","Cobblestone"};
    auto i=unsigned(b);return i<names.size()?names[i]:"Invalid";
}

Color blockColor(BlockType block) noexcept {
    switch (block) {
    case BlockType::Bedrock: return {0.12f, 0.12f, 0.14f};
    case BlockType::Grass: return {0.30f, 0.72f, 0.22f};
    case BlockType::Dirt: return {0.48f, 0.27f, 0.12f};
    case BlockType::Stone: return {0.48f, 0.52f, 0.57f};
    default: break;
    }
    return {0.0f, 0.0f, 0.0f};
}

} // namespace bloxide
