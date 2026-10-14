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
enum class GenerationState {Requested,Generating,Ready,Cancelled,Failed};

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
    std::array<BlockState, Volume> blocks{};
    std::uint64_t version = 0;
    bool edited = false;
    std::array<std::uint16_t,18*18> heightHalo{};
    std::array<std::uint16_t,16> sectionSolid{};
    std::array<std::uint32_t,static_cast<unsigned>(BlockType::Count)> materialCounts{};
    std::array<std::uint32_t,16> fluidCounts{};
    std::uint32_t treeIncidences=0,villageIncidences=0;
    ChunkData(){materialCounts[0]=Volume;}
    void rebuildSummaries() noexcept;
    [[nodiscard]] bool inBounds(int x, int y, int z) const noexcept;
    [[nodiscard]] BlockType get(int x, int y, int z) const noexcept;
    [[nodiscard]] BlockState getState(int x,int y,int z)const noexcept;
    bool setState(int x,int y,int z,BlockState value)noexcept;
    bool set(int x, int y, int z, BlockType value) noexcept;
    [[nodiscard]] std::size_t index(int x, int y, int z) const noexcept;
};

struct ChunkMeshVertex { std::uint32_t packed = 0; };
struct HeightVertex { float x=0,y=0,z=0,fromY=0; std::uint32_t layer=0,face=2; };
struct SurfaceVertex {
    float x=0,y=0,z=0,fromX=0,fromY=0,fromZ=0;
    std::uint32_t layer=0,face=2,ao=0;
    std::uint32_t anchor=0;
};
struct SurfaceFace {std::uint32_t first=0,count=6;float x=0,y=0,z=0;};

struct ChunkMesh {
    std::shared_ptr<void> uploadLease;
    ChunkCoord coord{};
    std::vector<ChunkMeshVertex> vertices;
    std::vector<HeightVertex> heightVertices;
    std::vector<ChunkMeshVertex> cutoutVertices;
    std::vector<std::uint32_t> cutoutIndices;
    std::vector<SurfaceVertex> plantVertices,translucentVertices;
    std::vector<SurfaceVertex> surfaceVertices;
    std::vector<std::uint32_t> surfaceIndices,coarseIndices;
    std::vector<SurfaceVertex> compactVertices;
    std::vector<std::uint32_t> compactIndices;
    std::vector<std::uint32_t> plantIndices,translucentIndices;
    std::vector<SurfaceFace> translucentFaces;
    bool heightmap=false;
    bool surface3D=false;
    std::uint32_t collapseAccepted=0,collapseRejected=0,refinedVertices=0;
    std::vector<std::uint32_t> indices;
    std::uint8_t lod = 0;
    std::uint64_t sourceVersion = 0;
    std::uint64_t instanceId = 0;
    std::uint64_t revision = 0;
    std::uint64_t groupId = 0;
    std::size_t byteSize() const noexcept {
        return (vertices.size()+cutoutVertices.size())*sizeof(ChunkMeshVertex)+heightVertices.size()*sizeof(HeightVertex)
            +(plantVertices.size()+translucentVertices.size())*sizeof(SurfaceVertex)
            +surfaceVertices.size()*sizeof(SurfaceVertex)
            +(indices.size()+cutoutIndices.size()+plantIndices.size()+translucentIndices.size()+surfaceIndices.size()+coarseIndices.size())*sizeof(std::uint32_t);
    }
    std::size_t residentByteSize()const noexcept{
        return sizeof(ChunkMesh)+(vertices.capacity()+cutoutVertices.capacity())*sizeof(ChunkMeshVertex)+heightVertices.capacity()*sizeof(HeightVertex)
            +(plantVertices.capacity()+translucentVertices.capacity()+surfaceVertices.capacity()+compactVertices.capacity())*sizeof(SurfaceVertex)
            +(indices.capacity()+cutoutIndices.capacity()+plantIndices.capacity()+translucentIndices.capacity()+surfaceIndices.capacity()+coarseIndices.capacity()+compactIndices.capacity())*sizeof(std::uint32_t)
            +translucentFaces.capacity()*sizeof(SurfaceFace);
    }
};

struct ChunkRecord {
    explicit ChunkRecord(ChunkCoord value) : coord(value) {}
    ChunkCoord coord;
    std::shared_ptr<ChunkData> data;
    std::shared_ptr<ChunkMesh> pendingMesh;
    std::shared_ptr<ChunkMesh> activeMesh;
    std::uint64_t instanceId = 0;
    std::uint64_t groupId = 0;
    bool uploaded = false;
    std::uint8_t desiredLod = 0;
    std::atomic<ChunkState> state{ChunkState::Unloaded};
    std::atomic<unsigned char> dirty{static_cast<unsigned char>(ChunkDirty::Terrain) | static_cast<unsigned char>(ChunkDirty::Mesh)};
    std::atomic<std::uint64_t> version{0};
    std::atomic<bool> cancelRequested{false};
    std::atomic<GenerationState> generationState{GenerationState::Requested};
    std::uint64_t generationRevision=1;
    std::uint8_t retries = 0;
};

[[nodiscard]] ChunkCoord worldToChunk(float x, float z) noexcept;
[[nodiscard]] int floorMod(int value, int divisor) noexcept;
[[nodiscard]] int worldToLocal(float value) noexcept;

} // namespace bloxide
