#include "bloxide/World.hpp"

#include <cassert>

int main() {
    bloxide::World world;
    assert(world.inBounds(0, 0, 0));
    assert(!world.inBounds(-1, 0, 0));
    assert(world.get(4, 0, 4) == bloxide::BlockType::Stone);
    assert(world.get(4, 1, 4) == bloxide::BlockType::Dirt);
    assert(world.get(4, 2, 4) == bloxide::BlockType::Grass);
    assert(world.get(4, 3, 4) == bloxide::BlockType::Air);
    assert(world.set(4, 3, 4, bloxide::BlockType::Stone));
    assert(world.get(4, 3, 4) == bloxide::BlockType::Stone);
    assert(!world.set(-1, 0, 0, bloxide::BlockType::Stone));
    const auto hit = world.raycast({4.5f, 4.5f, 8.5f}, {0.0f, -0.15f, -1.0f}, 8.0f);
    assert(hit.hit);
    return 0;
}
