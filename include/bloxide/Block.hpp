#pragma once

#include <array>

namespace bloxide {

enum class BlockType : unsigned char {
    Air = 0,
    Bedrock,
    Grass,
    Dirt,
    Stone
};

struct Color {
    float r;
    float g;
    float b;
};

[[nodiscard]] bool isSolid(BlockType block) noexcept;
[[nodiscard]] bool isBreakable(BlockType block) noexcept;
[[nodiscard]] Color blockColor(BlockType block) noexcept;

} // namespace bloxide
