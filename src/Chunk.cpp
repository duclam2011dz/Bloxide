#include "bloxide/Chunk.hpp"

#include <cmath>

namespace bloxide {

std::size_t ChunkCoordHash::operator()(const ChunkCoord& coord) const noexcept {
    std::uint64_t x = static_cast<std::uint64_t>(coord.x);
    std::uint64_t z = static_cast<std::uint64_t>(coord.z);
    x ^= x >> 30U; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 27U;
    z ^= z >> 30U; z *= 0x94d049bb133111ebULL; z ^= z >> 27U;
    return static_cast<std::size_t>(x ^ (z + 0x9e3779b97f4a7c15ULL + (x << 6U) + (x >> 2U)));
}

bool ChunkData::inBounds(int x, int y, int z) const noexcept { return x >= 0 && x < Width && y >= 0 && y < Height && z >= 0 && z < Depth; }
std::size_t ChunkData::index(int x, int y, int z) const noexcept { return (static_cast<std::size_t>(y) * Depth + static_cast<std::size_t>(z)) * Width + static_cast<std::size_t>(x); }
BlockType ChunkData::get(int x, int y, int z) const noexcept { return inBounds(x, y, z) ? blocks[index(x, y, z)] : BlockType::Air; }
bool ChunkData::set(int x, int y, int z, BlockType value) noexcept { if (!inBounds(x, y, z) || (y == 0 && value != BlockType::Bedrock)) return false; blocks[index(x, y, z)] = value; ++version; return true; }

int floorMod(int value, int divisor) noexcept { const int result = value % divisor; return result < 0 ? result + divisor : result; }
int worldToLocal(float value) noexcept { return floorMod(static_cast<int>(std::floor(value)), ChunkData::Width); }
ChunkCoord worldToChunk(float x, float z) noexcept { return {static_cast<std::int64_t>(std::floor(x / 16.0f)), static_cast<std::int64_t>(std::floor(z / 16.0f))}; }

} // namespace bloxide
