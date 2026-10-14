#pragma once
#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <bloxide/Resources.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <functional>
namespace bloxide {
using ChunkEdits=std::unordered_map<std::uint32_t,BlockState>;
struct FarVoxelSnapshot {
    std::shared_ptr<const ChunkData> center;
    ChunkNeighbors neighbors;
    std::array<std::shared_ptr<const ChunkEdits>,5> edits{};
};
struct HeightTileInput {
    ChunkCoord coord{};int step=2;
    std::array<std::array<float,17>,4> edges{};
    std::array<bool,4> voxelEdge{};
    std::array<bool,4> skirts{true,true,true,true};
    std::shared_ptr<const ChunkMesh> previous;
    float previousMorph=1;
    std::array<std::shared_ptr<const ChunkEdits>,5> edits{};
};
class HeightmapMesher {
public:
    static int selectStep(int radius,int previous=0);
    static std::shared_ptr<ChunkMesh> build(const ChunkGenerator&,const HeightTileInput&,const std::atomic<bool>* cancel=nullptr);
    static float heightOnMesh(const ChunkMesh&,float x,float z,float morph=1);
};
// Main-thread tile ownership. Generation workers acquire immutable inputs briefly.
class HeightmapStream {
public:
    struct Tile {
        HeightTileInput input;std::uint64_t instance=0,revision=1,group=0;
        std::shared_ptr<ChunkMesh> active,pending;
        std::shared_ptr<std::atomic<bool>> cancel=std::make_shared<std::atomic<bool>>(false);
        bool uploaded=false;std::atomic<GenerationState> state{GenerationState::Requested};
        unsigned retries=0;
        bool enqueued=false;
        std::chrono::steady_clock::time_point published{};
    };
    using BorderQuery=std::function<std::optional<float>(std::int64_t,std::int64_t)>;
    using SnapshotFactory=std::function<FarVoxelSnapshot(ChunkCoord)>;
    void configureAdaptive(bool value){adaptive_=value;initialized_=false;}
    void setLimits(int radius,std::size_t generationSlots,std::size_t meshSlots,std::size_t memoryHeadroom);
    void attachCredits(std::shared_ptr<QueueCredits> generation,std::shared_ptr<QueueCredits> meshing){generationCredits_=std::move(generation);meshCredits_=std::move(meshing);}
    void attachUploads(std::shared_ptr<QueueCredits> credits){uploadCredits_=std::move(credits);}
    void update(ChunkCoord center,const BorderQuery&,const std::array<float,3>& direction={0,0,-1},
        const std::function<bool(ChunkCoord)>& voxelVisible={},std::uint64_t borderRevision=0,const SnapshotFactory& snapshots={});
    bool hasJobs()const;
    bool work(const ChunkGenerator&);
    bool hasMeshJobs()const;
    bool workMeshing(const ChunkMesher&);
    void stop();
    void voxelPublished(ChunkCoord);
    std::vector<std::shared_ptr<Tile>> pending()const;
    std::vector<std::shared_ptr<Tile>> ready()const;
    std::vector<std::vector<std::shared_ptr<Tile>>> readyBatches()const;
    bool commit(const std::vector<std::shared_ptr<Tile>>&);
    std::vector<std::shared_ptr<Tile>> visible(ChunkCoord center)const;
    std::vector<ChunkCoord> coords()const;
    float morph(const Tile&)const;
    std::size_t tileCount()const{return tiles_.size();}
    std::size_t desiredCount(ChunkCoord center)const{
        std::size_t count=0;for(auto&[c,t]:tiles_)if(std::max(std::llabs(c.x-center.x),std::llabs(c.z-center.z))>8)++count;return count;
    }
    bool settled()const;
    std::size_t residentBytes(const std::vector<const ChunkData*>& alreadyCounted={})const;
    std::uint64_t stale=0,cancelled=0,completed=0;
    double buildMs=0;
    double generationMs=0;
    std::size_t reservedBytes()const{return reservedBytes_.load();}
    bool adaptive()const{return adaptive_;}
    bool memoryPressure()const;
private:
    struct Job {std::shared_ptr<Tile> tile;HeightTileInput input;std::uint64_t revision,group;int priority;std::chrono::steady_clock::time_point queued;std::shared_ptr<std::atomic<bool>> cancel;FarVoxelSnapshot source;};
    struct MeshTask {Job job;std::shared_ptr<const ChunkData> center;ChunkNeighbors neighbors;std::uint64_t signature=0;double generationMs=0;};
    struct Result {Job job;std::shared_ptr<ChunkMesh> mesh;bool failed=false;double ms=0,generationMs=0;};
    std::unordered_map<ChunkCoord,std::shared_ptr<Tile>,ChunkCoordHash> tiles_;
    mutable std::mutex mutex_;std::vector<Job> jobs_;std::vector<Result> results_;
    std::vector<MeshTask> meshTasks_;
    std::uint64_t nextInstance_=1,nextGroup_=1,group_=0;
    ChunkCoord lastCenter_{};std::uint64_t lastBorderRevision_=0;bool initialized_=false;
    bool adaptive_=false;
    int radius_=24;
    std::size_t generationSlots_=128,meshSlots_=64;
    std::atomic<std::size_t> memoryHeadroom_{512*1024*1024},reservedBytes_{0};
    std::shared_ptr<QueueCredits> generationCredits_,meshCredits_;
    std::shared_ptr<QueueCredits> uploadCredits_;
    void refill(ChunkCoord,const std::array<float,3>&,const SnapshotFactory&);
};
}
