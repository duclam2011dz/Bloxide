#pragma once

#include "bloxide/Block.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bloxide {

struct ChunkCoord {
    std::int64_t x = 0;
    std::int64_t z = 0;
    friend bool operator==(const ChunkCoord& a, const ChunkCoord& b) noexcept { return a.x == b.x && a.z == b.z; }
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const noexcept;
};

enum class ChunkState : unsigned char {
    Unloaded, Queued, Loading, Generated, Meshing, Uploading, Ready, Unloading, Failed
};

enum class ChunkDirty : unsigned char {
    None = 0, Terrain = 1 << 0, Neighbor = 1 << 1, Mesh = 1 << 2, Upload = 1 << 3
};

constexpr ChunkDirty operator|(ChunkDirty a, ChunkDirty b) noexcept { return static_cast<ChunkDirty>(static_cast<unsigned char>(a) | static_cast<unsigned char>(b)); }
constexpr bool hasDirty(ChunkDirty flags, ChunkDirty value) noexcept { return (static_cast<unsigned char>(flags) & static_cast<unsigned char>(value)) != 0; }

struct ChunkData {
    static constexpr int Width = 16;
    static constexpr int Height = 256;
    static constexpr int Depth = 16;
    static constexpr std::size_t Volume = static_cast<std::size_t>(Width) * Height * Depth;
    std::array<BlockType, Volume> blocks{};
    std::uint64_t version = 0;
    [[nodiscard]] bool inBounds(int x, int y, int z) const noexcept;
    [[nodiscard]] BlockType get(int x, int y, int z) const noexcept;
    bool set(int x, int y, int z, BlockType value) noexcept;
    [[nodiscard]] std::size_t index(int x, int y, int z) const noexcept;
};

struct ChunkMeshVertex { std::uint32_t packed = 0; };

struct ChunkMesh {
    ChunkCoord coord{};
    std::vector<ChunkMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint8_t lod = 0;
    std::uint64_t sourceVersion = 0;
    std::size_t byteSize() const noexcept { return vertices.size() * sizeof(ChunkMeshVertex) + indices.size() * sizeof(std::uint32_t); }
};

struct ChunkRecord {
    explicit ChunkRecord(ChunkCoord value) : coord(value) {}
    ChunkCoord coord;
    std::shared_ptr<ChunkData> data;
    std::shared_ptr<ChunkMesh> mesh;
    std::atomic<ChunkState> state{ChunkState::Unloaded};
    std::atomic<unsigned char> dirty{static_cast<unsigned char>(ChunkDirty::Terrain) | static_cast<unsigned char>(ChunkDirty::Mesh)};
    std::atomic<std::uint64_t> version{0};
    std::atomic<bool> cancelRequested{false};
    std::uint8_t retries = 0;
};

[[nodiscard]] ChunkCoord worldToChunk(float x, float z) noexcept;
[[nodiscard]] int floorMod(int value, int divisor) noexcept;
[[nodiscard]] int worldToLocal(float value) noexcept;

} // namespace bloxide
