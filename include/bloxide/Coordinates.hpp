#pragma once
#include <bloxide/Chunk.hpp>
#include <array>
#include <cmath>
#include <stdexcept>
namespace bloxide {
inline constexpr std::int64_t WorldLimit = std::int64_t{1} << 40;
struct WorldPosition {
    ChunkCoord chunk{};
    double x = 8, y = 80, z = 8;
    static WorldPosition fromAbsolute(double x, double y, double z) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            std::abs(x) > WorldLimit || std::abs(z) > WorldLimit || std::abs(y) > 1024)
            throw std::out_of_range("world coordinate outside +/-2^40");
        ChunkCoord c{static_cast<std::int64_t>(std::floor(x / 16)),
                     static_cast<std::int64_t>(std::floor(z / 16))};
        return {c, x - c.x * 16, y, z - c.z * 16};
    }
    std::int64_t blockX() const {
        auto p = *this;
        p.normalize();
        return p.chunk.x * 16 + static_cast<std::int64_t>(std::floor(p.x));
    }
    std::int64_t blockZ() const {
        auto p = *this;
        p.normalize();
        return p.chunk.z * 16 + static_cast<std::int64_t>(std::floor(p.z));
    }
    std::array<double, 3> local() const {
        return {x, y, z};
    }
    void normalize() {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(y) > 1024)
            throw std::out_of_range("nonfinite/out-of-height position");
        if (std::abs(x) > 1024 || std::abs(z) > 1024 || chunk.x > WorldLimit / 16 ||
            chunk.x < -WorldLimit / 16 || chunk.z > WorldLimit / 16 || chunk.z < -WorldLimit / 16)
            throw std::out_of_range("invalid local world position");
        const auto dx = static_cast<std::int64_t>(std::floor(x / 16)),
                   dz = static_cast<std::int64_t>(std::floor(z / 16));
        chunk.x += dx;
        chunk.z += dz;
        x -= dx * 16;
        z -= dz * 16;
        if (chunk.x > WorldLimit / 16 || chunk.x < -WorldLimit / 16 || chunk.z > WorldLimit / 16 ||
            chunk.z < -WorldLimit / 16 || (chunk.x == WorldLimit / 16 && x > 0) ||
            (chunk.z == WorldLimit / 16 && z > 0))
            throw std::out_of_range("world boundary reached");
    }
};
inline ChunkCoord blockToChunk(std::int64_t x, std::int64_t z) {
    return {x / 16 - (x % 16 < 0), z / 16 - (z % 16 < 0)};
}
inline int blockLocal(std::int64_t v) {
    return static_cast<int>((v % 16 + 16) % 16);
}
} // namespace bloxide
