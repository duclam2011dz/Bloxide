#pragma once
#include <bloxide/ChunkGenerator.hpp>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <functional>
namespace bloxide {
struct HeightTileInput {
    ChunkCoord coord{};int step=2;
    std::array<std::array<float,17>,4> edges{};
    std::array<bool,4> voxelEdge{};
    std::array<bool,4> skirts{true,true,true,true};
    std::shared_ptr<const ChunkMesh> previous;
    float previousMorph=1;
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
        std::chrono::steady_clock::time_point published{};
    };
    using BorderQuery=std::function<std::optional<float>(std::int64_t,std::int64_t)>;
    void update(ChunkCoord center,const BorderQuery&,const std::array<float,3>& direction={0,0,-1},
        const std::function<bool(ChunkCoord)>& voxelVisible={},std::uint64_t borderRevision=0);
    bool hasJobs()const;
    bool work(const ChunkGenerator&);
    void stop();
    void voxelPublished(ChunkCoord);
    std::vector<std::shared_ptr<Tile>> pending()const;
    std::vector<std::shared_ptr<Tile>> ready()const;
    bool commit(const std::vector<std::shared_ptr<Tile>>&);
    std::vector<std::shared_ptr<Tile>> visible(ChunkCoord center)const;
    std::vector<ChunkCoord> coords()const;
    float morph(const Tile&)const;
    std::size_t tileCount()const{return tiles_.size();}
    bool settled()const;
    std::uint64_t stale=0,cancelled=0,completed=0;
    double buildMs=0;
private:
    struct Job {std::shared_ptr<Tile> tile;HeightTileInput input;std::uint64_t revision,group;int priority;std::chrono::steady_clock::time_point queued;std::shared_ptr<std::atomic<bool>> cancel;};
    struct Result {Job job;std::shared_ptr<ChunkMesh> mesh;bool failed=false;double ms=0;};
    std::unordered_map<ChunkCoord,std::shared_ptr<Tile>,ChunkCoordHash> tiles_;
    mutable std::mutex mutex_;std::vector<Job> jobs_;std::vector<Result> results_;
    std::uint64_t nextInstance_=1,nextGroup_=1,group_=0;
    ChunkCoord lastCenter_{};std::uint64_t lastBorderRevision_=0;bool initialized_=false;
};
}
