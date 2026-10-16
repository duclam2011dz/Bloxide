#pragma once
#include <bloxide/Block.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <bloxide/Coordinates.hpp>
namespace bloxide {
struct Int3 {
    std::int64_t x = 0;
    int y = 0;
    std::int64_t z = 0;
    bool operator==(const Int3& b) const {
        return x == b.x && y == b.y && z == b.z;
    }
};
struct BlockTarget {
    bool hit = false;
    Int3 block{}, previous{}, normal{};
    double distance = 0;
    BlockType type = BlockType::Air;
    std::uint64_t instanceId = 0, revision = 0;
};
struct TargetVoxel {
    BlockType type = BlockType::Air;
    bool blocked = false;
    std::uint64_t instanceId = 0, revision = 0;
};
BlockTarget traceVoxels(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                        float reach,
                        const std::function<TargetVoxel(std::int64_t, int, std::int64_t)>& query);
BlockTarget
traceWorldVoxels(const WorldPosition& origin, const std::array<float, 3>& direction, float reach,
                 const std::function<TargetVoxel(std::int64_t, int, std::int64_t)>& query);
} // namespace bloxide
