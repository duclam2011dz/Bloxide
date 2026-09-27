#pragma once

#include "bloxide/Chunk.hpp"
#include "bloxide/ChunkGenerator.hpp"
#include "bloxide/ChunkMesher.hpp"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bloxide {

struct Int3 { int x; int y; int z; };
struct RaycastHit { bool hit = false; Int3 block{0, 0, 0}; Int3 previous{0, 0, 0}; float distance = 0.0f; };

struct ChunkUpload { ChunkCoord coord{}; std::shared_ptr<ChunkMesh> mesh; };
struct RenderChunk { ChunkCoord coord{}; std::shared_ptr<const ChunkMesh> mesh; };

struct WorldStats {
    std::size_t loaded = 0, generated = 0, meshed = 0, ready = 0, visible = 0;
    std::size_t generationQueue = 0, meshingQueue = 0, vertices = 0, indices = 0;
    std::uint64_t generationJobs = 0, meshingJobs = 0, cancelledJobs = 0, failedJobs = 0;
};

class World {
public:
    static constexpr int GenerationDistance = 10;
    static constexpr int SimulationDistance = 6;
    static constexpr int RenderDistance = 8;

    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void update(const std::array<float, 3>& playerPosition, const std::array<float, 3>& viewDirection);
    [[nodiscard]] std::vector<ChunkUpload> takePendingUploads();
    void markUploaded(const ChunkCoord& coord, std::uint64_t version);
    [[nodiscard]] std::vector<RenderChunk> renderChunks(const std::array<float, 3>& playerPosition,
                                                         const std::array<float, 3>& viewDirection) const;

    [[nodiscard]] BlockType get(int worldX, int y, int worldZ) const;
    bool set(int worldX, int y, int worldZ, BlockType block);
    [[nodiscard]] RaycastHit raycast(const std::array<float, 3>& origin,
                                      const std::array<float, 3>& direction,
                                      float maxDistance) const noexcept;
    [[nodiscard]] WorldStats stats() const;

private:
    struct GenerateJob { int priority; ChunkCoord coord; std::shared_ptr<ChunkRecord> record; std::uint64_t version; };
    struct MeshJob { int priority; ChunkCoord coord; std::shared_ptr<ChunkRecord> record; std::shared_ptr<const ChunkData> center; ChunkNeighbors neighbors; std::uint8_t lod; std::uint64_t version; };
    struct GenerateResult { ChunkCoord coord; std::shared_ptr<ChunkRecord> record; std::shared_ptr<ChunkData> data; std::uint64_t version; };
    struct MeshResult { ChunkCoord coord; std::shared_ptr<ChunkRecord> record; std::shared_ptr<ChunkMesh> mesh; std::uint64_t version; };
    struct GenerateCompare { bool operator()(const GenerateJob& a, const GenerateJob& b) const noexcept { return a.priority > b.priority; } };
    struct MeshCompare { bool operator()(const MeshJob& a, const MeshJob& b) const noexcept { return a.priority > b.priority; } };

    void generationLoop();
    void meshingLoop();
    void pumpResults();
    void scheduleGeneration(const ChunkCoord& coord, int priority);
    void scheduleMeshing(const std::shared_ptr<ChunkRecord>& record, int priority, std::uint8_t lod);
    [[nodiscard]] int priorityFor(const ChunkCoord& coord, const ChunkCoord& player, const std::array<float, 3>& direction) const noexcept;
    [[nodiscard]] std::shared_ptr<ChunkRecord> find(const ChunkCoord& coord) const;
    void applyJournal(const ChunkCoord& coord, ChunkData& data);
    void markNeighborMeshes(const ChunkCoord& coord);

    mutable std::mutex mapMutex_;
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkRecord>, ChunkCoordHash> chunks_;
    std::unordered_map<ChunkCoord, std::unordered_map<std::uint32_t, BlockType>, ChunkCoordHash> edits_;
    mutable std::mutex generationMutex_, meshingMutex_, resultMutex_, statsMutex_;
    std::condition_variable generationCv_, meshingCv_;
    std::priority_queue<GenerateJob, std::vector<GenerateJob>, GenerateCompare> generationQueue_;
    std::priority_queue<MeshJob, std::vector<MeshJob>, MeshCompare> meshingQueue_;
    std::vector<GenerateResult> generationResults_;
    std::vector<MeshResult> meshingResults_;
    std::vector<std::thread> generationThreads_, meshingThreads_;
    bool stopping_ = false;
    ChunkGenerator generator_;
    ChunkMesher mesher_;
    ChunkCoord playerChunk_{};
    std::array<float, 3> viewDirection_{0.0f, 0.0f, -1.0f};
    mutable WorldStats stats_{};
};

} // namespace bloxide
