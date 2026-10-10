#pragma once
#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <bloxide/Targeting.hpp>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

namespace bloxide {
using RaycastHit=BlockTarget;
struct ChunkUpload { ChunkCoord coord{}; std::shared_ptr<ChunkMesh> mesh; };
struct RenderChunk { ChunkCoord coord{}; std::shared_ptr<const ChunkMesh> mesh; };
struct UploadGroup { std::uint64_t id=0; std::vector<ChunkUpload> members; };
struct WorldStats {
    std::size_t loaded=0,generated=0,meshed=0,ready=0,visible=0;
    std::size_t generationQueue=0,meshingQueue=0,vertices=0,indices=0;
    std::uint64_t generationJobs=0,meshingJobs=0,cancelledJobs=0,failedJobs=0,swaps=0,staleResults=0;
    double lastPublishMs=0;
};
class World {
public:
    static constexpr int GenerationDistance=10,SimulationDistance=6,RenderDistance=8;
    World();
    ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    void update(const std::array<float,3>& position,const std::array<float,3>& direction);
    std::vector<ChunkUpload> takePendingUploads();
    bool markUploaded(const ChunkUpload& upload);
    std::vector<UploadGroup> readyGroups() const;
    bool commitGroup(const UploadGroup& group);
    std::vector<RenderChunk> renderChunks(const std::array<float,3>& position,const std::array<float,3>& direction) const;
    std::vector<ChunkCoord> loadedCoords() const;
    BlockType get(int x,int y,int z) const;
    bool set(int x,int y,int z,BlockType block);
    bool columnLoaded(int x,int z) const;
    bool targetable(int x,int z) const;
    BlockTarget target(const std::array<float,3>& origin,const std::array<float,3>& direction,float reach=6) const;
    bool validateTarget(const BlockTarget& presented,const std::array<float,3>& origin,const std::array<float,3>& direction) const;
    RaycastHit raycast(const std::array<float,3>& origin,const std::array<float,3>& direction,float reach) const;
    WorldStats stats() const;
    void setMeshDelayForTests(int ms) { meshDelayMs_=ms; }
    void failNextMeshForTests() { failMeshes_=1; }
private:
    using Clock=std::chrono::steady_clock;
    struct GenerateJob { int priority; std::shared_ptr<ChunkRecord> record; };
    struct MeshJob {
        int priority; std::shared_ptr<ChunkRecord> record; std::shared_ptr<const ChunkData> center;
        ChunkNeighbors neighbors; std::uint64_t revision,group; std::uint8_t lod;
    };
    struct GenerateResult { std::shared_ptr<ChunkRecord> record; std::shared_ptr<ChunkData> data; };
    struct MeshResult { std::shared_ptr<ChunkRecord> record; std::shared_ptr<ChunkMesh> mesh; std::uint64_t revision; };
    struct GenerateCompare { bool operator()(const GenerateJob&a,const GenerateJob&b)const{return a.priority>b.priority;} };
    struct MeshCompare { bool operator()(const MeshJob&a,const MeshJob&b)const{return a.priority>b.priority;} };
    struct Group { std::vector<ChunkCoord> members; Clock::time_point started; bool edit=false; };
    void generationLoop();
    void meshingLoop();
    void pumpResults();
    void scheduleGeneration(ChunkCoord coord,int priority);
    void scheduleMeshing(const std::shared_ptr<ChunkRecord>& record,int priority);
    std::uint64_t invalidateGroup(std::vector<ChunkCoord> coords,bool edit=false);
    int priorityFor(ChunkCoord coord,ChunkCoord player,const std::array<float,3>& direction) const;
    std::shared_ptr<ChunkRecord> find(ChunkCoord coord) const;
    void applyJournal(ChunkCoord coord,ChunkData& data);
    // Map, records and journals have main-thread ownership; workers only see snapshots.
    std::unordered_map<ChunkCoord,std::shared_ptr<ChunkRecord>,ChunkCoordHash> chunks_;
    std::unordered_map<ChunkCoord,std::unordered_map<std::uint32_t,BlockType>,ChunkCoordHash> edits_;
    std::unordered_map<std::uint64_t,Group> groups_;
    std::uint64_t nextInstance_=1,nextGroup_=1;
    mutable std::mutex generationMutex_,meshingMutex_,resultMutex_,statsMutex_;
    std::condition_variable generationCv_,meshingCv_;
    std::priority_queue<GenerateJob,std::vector<GenerateJob>,GenerateCompare> generationQueue_;
    std::priority_queue<MeshJob,std::vector<MeshJob>,MeshCompare> meshingQueue_;
    std::vector<GenerateResult> generationResults_;
    std::vector<MeshResult> meshingResults_;
    std::vector<std::thread> generationThreads_,meshingThreads_;
    std::atomic<bool> stopping_{false};
    std::atomic<int> meshDelayMs_{0};
    std::atomic<int> failMeshes_{0};
    ChunkGenerator generator_;
    ChunkMesher mesher_;
    ChunkCoord playerChunk_{};
    std::array<float,3> viewDirection_{0,0,-1};
    mutable WorldStats stats_{};
};
}
