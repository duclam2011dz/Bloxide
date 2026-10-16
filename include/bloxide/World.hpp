#pragma once
#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <bloxide/Targeting.hpp>
#include <bloxide/Heightmap.hpp>
#include <bloxide/Fluid.hpp>
#include <bloxide/Resources.hpp>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace bloxide {
using RaycastHit = BlockTarget;
struct ChunkUpload {
    ChunkCoord coord{};
    std::shared_ptr<ChunkMesh> mesh;
};
struct RenderChunk {
    ChunkCoord coord{};
    std::shared_ptr<const ChunkMesh> mesh;
    float morph = 1;
};
struct UploadGroup {
    std::uint64_t id = 0;
    std::vector<ChunkUpload> members;
    std::vector<std::uint64_t> linkedGroups;
};
struct ChunkInspection {
    ChunkState state{};
    GenerationState generation{};
    std::uint64_t instance = 0, revision = 0, generationRevision = 0, group = 0, activeRevision = 0,
                  pendingRevision = 0;
    bool data = false, active = false, pending = false, uploaded = false;
};
struct WorldStats {
    std::size_t loaded = 0, generated = 0, meshed = 0, ready = 0, visible = 0;
    std::size_t generationQueue = 0, meshingQueue = 0, vertices = 0, indices = 0;
    std::uint64_t generationJobs = 0, meshingJobs = 0, cancelledJobs = 0, failedJobs = 0, swaps = 0,
                  staleResults = 0;
    std::uint64_t retriedJobs = 0, farCancelled = 0;
    double lastPublishMs = 0;
    double generationCpuMs = 0, meshingCpuMs = 0, heightmapCpuMs = 0;
    std::uint64_t heightmapJobs = 0, heightmapStale = 0;
    std::size_t pendingUploads = 0, farPendingUploads = 0, farGroupReady = 0, publicationGroups = 0;
    std::size_t cpuCacheBytes = 0, journalPayloadBytes = 0;
    std::size_t cpuReservations = 0;
    std::size_t uploadStaging = 0;
    std::uint64_t deferredGeneration = 0, deferredMeshing = 0;
    std::array<std::uint64_t, static_cast<unsigned>(BlockType::Count)> blockCounts{};
    std::array<std::uint64_t, 16> fluidCounts{};
    std::uint64_t treeIncidences = 0, villageIncidences = 0;
    std::uint64_t collapses = 0, rejectedCollapses = 0, refinedVertices = 0;
    std::array<std::size_t, 3> lodTiles{};
    std::size_t morphingTiles = 0;
};
class World {
  public:
    static constexpr int GenerationDistance = 10, SimulationDistance = 6, RenderDistance = 8;
    explicit World(const WorldSettings& settings = {}, bool heightmap = false,
                   NoiseBackend backend = NoiseBackend::Auto);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    void update(const std::array<float, 3>& position, const std::array<float, 3>& direction);
    void updateAt(const WorldPosition& position, const std::array<float, 3>& direction);
    std::vector<ChunkUpload> takePendingUploads();
    bool markUploaded(const ChunkUpload& upload);
    std::vector<UploadGroup> readyGroups() const;
    bool commitGroup(const UploadGroup& group);
    std::vector<RenderChunk> renderChunks(const std::array<float, 3>& position,
                                          const std::array<float, 3>& direction) const;
    std::vector<RenderChunk> renderChunksAt(const WorldPosition& position,
                                            const std::array<float, 3>& direction) const;
    std::vector<ChunkCoord> loadedCoords() const;
    std::vector<ChunkCoord> gpuCoords() const;
    std::optional<ChunkInspection> inspectChunk(ChunkCoord) const;
    BlockType get(std::int64_t x, int y, std::int64_t z) const;
    BlockState getState(std::int64_t x, int y, std::int64_t z) const;
    bool setState(std::int64_t x, int y, std::int64_t z, BlockState block);
    void advanceSimulation(double dt);
    FluidStats fluidStats() const {
        return fluids_.stats();
    }
    const ResourceLimits& resourceLimits() const {
        return limits_;
    }
    void setResourceLimits(const ResourceLimits&);
    void reportGpuPressure(std::size_t bytes, double dt);
    int effectiveFarRadius() const {
        return heightmapEnabled_ ? distanceBudget_.effective : 8;
    }
    const char* distanceReason() const {
        return distanceReason_;
    }
    bool set(std::int64_t x, int y, std::int64_t z, BlockType block);
    bool columnLoaded(std::int64_t x, std::int64_t z) const;
    bool targetable(std::int64_t x, std::int64_t z) const;
    int surfaceHeight(std::int64_t x, std::int64_t z) const {
        return generator_.heightAt(x, z);
    }
    const ChunkGenerator& generator() const {
        return generator_;
    }
    const WorldSettings& settings() const {
        return generator_.settings();
    }
    std::size_t heightmapTiles() const {
        return far_.tileCount();
    }
    std::size_t expectedFarTiles() const {
        return far_.desiredCount(playerChunk_);
    }
    bool heightmapSettled() const {
        return !heightmapEnabled_ || far_.settled();
    }
    BlockTarget target(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                       float reach = 6) const;
    BlockTarget targetAt(const WorldPosition& origin, const std::array<float, 3>& direction,
                         float reach = 6) const;
    bool validateTarget(const BlockTarget& presented, const std::array<float, 3>& origin,
                        const std::array<float, 3>& direction) const;
    bool validateTargetAt(const BlockTarget& presented, const WorldPosition& origin,
                          const std::array<float, 3>& direction) const;
    RaycastHit raycast(const std::array<float, 3>& origin, const std::array<float, 3>& direction,
                       float reach) const;
    WorldStats stats() const;
    void setMeshDelayForTests(int ms) {
        meshDelayMs_ = ms;
    }
    void failNextMeshForTests() {
        failMeshes_ = 1;
    }

  private:
    using Clock = std::chrono::steady_clock;
    struct GenerateJob {
        int priority;
        std::shared_ptr<ChunkRecord> record;
        std::uint64_t instance, revision;
        double ageKey;
    };
    struct MeshJob {
        int priority;
        std::shared_ptr<ChunkRecord> record;
        std::shared_ptr<const ChunkData> center;
        ChunkNeighbors neighbors;
        std::uint64_t revision, group;
        std::uint8_t lod;
        std::shared_ptr<void> uploadLease;
    };
    struct GenerateResult {
        std::shared_ptr<ChunkRecord> record;
        std::shared_ptr<ChunkData> data;
        std::uint64_t instance, revision;
    };
    struct MeshResult {
        std::shared_ptr<ChunkRecord> record;
        std::shared_ptr<ChunkMesh> mesh;
        std::uint64_t revision;
    };
    struct GenerateCompare {
        bool operator()(const GenerateJob& a, const GenerateJob& b) const {
            return a.priority + a.ageKey > b.priority + b.ageKey;
        }
    };
    struct MeshCompare {
        bool operator()(const MeshJob& a, const MeshJob& b) const {
            return a.priority > b.priority;
        }
    };
    struct Group {
        std::vector<ChunkCoord> members;
        Clock::time_point started;
        bool edit = false;
    };
    void generationLoop();
    void meshingLoop();
    void pumpResults();
    void scheduleGeneration(ChunkCoord coord, int priority);
    void scheduleMeshing(const std::shared_ptr<ChunkRecord>& record, int priority);
    std::uint64_t invalidateGroup(std::vector<ChunkCoord> coords, bool edit = false);
    int priorityFor(ChunkCoord coord, ChunkCoord player,
                    const std::array<float, 3>& direction) const;
    std::shared_ptr<ChunkRecord> find(ChunkCoord coord) const;
    void applyJournal(ChunkCoord coord, ChunkData& data);
    void seedFluidChunk(ChunkCoord);
    void applyFluid(const std::vector<FluidUpdate>&);
    // Map, records and journals have main-thread ownership; workers only see snapshots.
    std::unordered_map<ChunkCoord, std::shared_ptr<ChunkRecord>, ChunkCoordHash> chunks_;
    std::unordered_map<ChunkCoord, std::unordered_map<std::uint32_t, BlockState>, ChunkCoordHash>
        edits_;
    std::unordered_map<ChunkCoord, std::shared_ptr<const ChunkEdits>, ChunkCoordHash>
        editSnapshots_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> dirtyEditSnapshots_;
    FarVoxelSnapshot captureFarSnapshot(ChunkCoord);
    std::unordered_map<std::uint64_t, Group> groups_;
    std::uint64_t nextInstance_ = 1, nextGroup_ = 1;
    mutable std::mutex generationMutex_, meshingMutex_, resultMutex_, statsMutex_;
    std::condition_variable generationCv_, meshingCv_;
    std::priority_queue<GenerateJob, std::vector<GenerateJob>, GenerateCompare> generationQueue_;
    std::priority_queue<MeshJob, std::vector<MeshJob>, MeshCompare> meshingQueue_;
    std::vector<GenerateResult> generationResults_;
    std::vector<MeshResult> meshingResults_;
    std::vector<std::thread> generationThreads_, meshingThreads_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> meshDelayMs_{0};
    std::atomic<int> failMeshes_{0};
    ChunkGenerator generator_;
    HeightmapStream far_;
    bool heightmapEnabled_ = false;
    std::uint64_t heightmapBoundaryRevision_ = 0;
    ChunkMesher mesher_;
    FluidEngine fluids_;
    ResourceLimits limits_{};
    std::shared_ptr<QueueCredits> generationCredits_ = std::make_shared<QueueCredits>(128),
                                  meshingCredits_ = std::make_shared<QueueCredits>(64);
    std::shared_ptr<QueueCredits> uploadCredits_ = std::make_shared<QueueCredits>(64);
    DistanceBudget distanceBudget_{};
    const char* distanceReason_ = "requested distance";
    std::unordered_set<ChunkCoord, ChunkCoordHash> fluidRegion_;
    ChunkCoord playerChunk_{};
    std::array<float, 3> viewDirection_{0, 0, -1};
    mutable WorldStats stats_{};
};
} // namespace bloxide
