#include "bloxide/Block.hpp"

namespace bloxide {

bool isSolid(BlockType block) noexcept {
    return block != BlockType::Air;
}

bool isBreakable(BlockType block) noexcept {
    return isSolid(block) && block != BlockType::Bedrock;
}

Color blockColor(BlockType block) noexcept {
    switch (block) {
    case BlockType::Bedrock: return {0.12f, 0.12f, 0.14f};
    case BlockType::Grass: return {0.30f, 0.72f, 0.22f};
    case BlockType::Dirt: return {0.48f, 0.27f, 0.12f};
    case BlockType::Stone: return {0.48f, 0.52f, 0.57f};
    case BlockType::Air: break;
    }
    return {0.0f, 0.0f, 0.0f};
}

} // namespace bloxide
