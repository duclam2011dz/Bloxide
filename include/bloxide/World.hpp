#pragma once

#include "bloxide/Block.hpp"

#include <array>
#include <cstddef>

namespace bloxide {

struct Int3 {
    int x;
    int y;
    int z;
};

struct RaycastHit {
    bool hit = false;
    Int3 block{0, 0, 0};
    Int3 previous{0, 0, 0};
    float distance = 0.0f;
};

class World {
public:
    static constexpr int Width = 32;
    static constexpr int Height = 16;
    static constexpr int Depth = 32;

    World();

    [[nodiscard]] bool inBounds(int x, int y, int z) const noexcept;
    [[nodiscard]] BlockType get(int x, int y, int z) const noexcept;
    bool set(int x, int y, int z, BlockType block) noexcept;
    [[nodiscard]] RaycastHit raycast(const std::array<float, 3>& origin,
                                      const std::array<float, 3>& direction,
                                      float maxDistance) const noexcept;

private:
    [[nodiscard]] std::size_t index(int x, int y, int z) const noexcept;
    std::array<BlockType, Width * Height * Depth> blocks_{};
};

} // namespace bloxide
