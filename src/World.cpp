#include "bloxide/World.hpp"

#include <algorithm>
#include <cmath>

namespace bloxide {

World::World() {
    blocks_.fill(BlockType::Air);
    for (int x = 0; x < Width; ++x) {
        for (int z = 0; z < Depth; ++z) {
            blocks_[index(x, 0, z)] = BlockType::Stone;
            blocks_[index(x, 1, z)] = BlockType::Dirt;
            blocks_[index(x, 2, z)] = BlockType::Grass;
        }
    }
}

bool World::inBounds(int x, int y, int z) const noexcept {
    return x >= 0 && x < Width && y >= 0 && y < Height && z >= 0 && z < Depth;
}

std::size_t World::index(int x, int y, int z) const noexcept {
    return static_cast<std::size_t>((y * Depth + z) * Width + x);
}

BlockType World::get(int x, int y, int z) const noexcept {
    return inBounds(x, y, z) ? blocks_[index(x, y, z)] : BlockType::Air;
}

bool World::set(int x, int y, int z, BlockType block) noexcept {
    if (!inBounds(x, y, z)) {
        return false;
    }
    blocks_[index(x, y, z)] = block;
    return true;
}

RaycastHit World::raycast(const std::array<float, 3>& origin,
                          const std::array<float, 3>& direction,
                          float maxDistance) const noexcept {
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length <= 0.0001f) {
        return {};
    }

    const std::array<float, 3> dir{direction[0] / length, direction[1] / length, direction[2] / length};
    Int3 previous{static_cast<int>(std::floor(origin[0])), static_cast<int>(std::floor(origin[1])), static_cast<int>(std::floor(origin[2]))};
    const float step = 0.04f;
    for (float distance = 0.0f; distance <= maxDistance; distance += step) {
        const int x = static_cast<int>(std::floor(origin[0] + dir[0] * distance));
        const int y = static_cast<int>(std::floor(origin[1] + dir[1] * distance));
        const int z = static_cast<int>(std::floor(origin[2] + dir[2] * distance));
        if (x == previous.x && y == previous.y && z == previous.z) {
            continue;
        }
        if (inBounds(x, y, z) && isSolid(get(x, y, z))) {
            return {true, {x, y, z}, previous, distance};
        }
        previous = {x, y, z};
    }
    return {};
}

} // namespace bloxide
