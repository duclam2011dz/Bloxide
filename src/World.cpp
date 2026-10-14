#include <bloxide/World.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <stdexcept>
#include <bloxide/Trace.hpp>
namespace bloxide {
namespace {
constexpr std::size_t NearWorkerReservation=128*MiB+2*sizeof(ChunkData);
ChunkCoord coordOf(std::int64_t x,std::int64_t z){return blockToChunk(x,z);}
int distance(ChunkCoord a,ChunkCoord b) {
    return static_cast<int>(std::min<std::int64_t>(1000000,std::max(std::llabs(a.x-b.x),std::llabs(a.z-b.z))));
}
std::array<ChunkCoord,4> adjacent(ChunkCoord c) { return {{{c.x-1,c.z},{c.x+1,c.z},{c.x,c.z-1},{c.x,c.z+1}}}; }
}
World::World(const WorldSettings& settings,bool heightmap,NoiseBackend backend):generator_(settings,backend),heightmapEnabled_(heightmap) {
    far_.configureAdaptive(heightmap);
    far_.attachCredits(generationCredits_,meshingCredits_);
    far_.attachUploads(uploadCredits_);
    for(int i=0;i<2;++i) generationThreads_.emplace_back(&World::generationLoop,this);
    for(int i=0;i<2;++i) meshingThreads_.emplace_back(&World::meshingLoop,this);
}
World::~World() {
    stopping_=true;far_.stop(); generationCv_.notify_all(); meshingCv_.notify_all();
    for(auto& t:generationThreads_) t.join();
    for(auto& t:meshingThreads_) t.join();
    far_.stop();
}
std::shared_ptr<ChunkRecord> World::find(ChunkCoord c)const {
    auto it=chunks_.find(c); return it==chunks_.end()?nullptr:it->second;
}
int World::priorityFor(ChunkCoord c,ChunkCoord p,const std::array<float,3>& dir)const {
    float dx=static_cast<float>(c.x-p.x),dz=static_cast<float>(c.z-p.z);
    float len=std::sqrt(dx*dx+dz*dz);
    return distance(c,p)*100-static_cast<int>(len>0?(dir[0]*dx+dir[2]*dz)/len*25:25);
}
void World::scheduleGeneration(ChunkCoord c,int priority) {
    auto r=find(c);
    if(r){
        if(r->data||r->state==ChunkState::Queued||r->state==ChunkState::Loading||r->state==ChunkState::Unloading||
            (r->generationState==GenerationState::Failed&&r->retries>3))return;
        if(r->generationState==GenerationState::Ready)++r->generationRevision;
        r->generationState=GenerationState::Requested;
    }else{
        r=std::make_shared<ChunkRecord>(c);
        r->instanceId=nextInstance_++;r->version=1;chunks_.emplace(c,r);
    }
    auto ageKey=std::chrono::duration<double>(Clock::now().time_since_epoch()).count()*100;
    {std::lock_guard lock(generationMutex_);
        if(!generationCredits_->acquire()){r->state=ChunkState::Unloaded;++stats_.deferredGeneration;return;}
        r->state=ChunkState::Queued;generationQueue_.push({priority,r,r->instanceId,r->generationRevision,ageKey});}
    generationCv_.notify_one();
}
void World::applyJournal(ChunkCoord c,ChunkData& data) {
    auto it=edits_.find(c); if(it==edits_.end())return;
    for(auto [key,value]:it->second) data.setState(key&15U,(key>>4U)&255U,(key>>12U)&15U,value);
}
std::uint64_t World::invalidateGroup(std::vector<ChunkCoord> coords,bool edit) {
    std::unordered_set<ChunkCoord,ChunkCoordHash> selected;
    std::unordered_set<std::uint64_t> oldGroups;
    auto started=Clock::now();
    for(std::size_t i=0;i<coords.size();++i) {
        if(heightmapEnabled_&&distance(coords[i],playerChunk_)>8)continue;
        auto r=find(coords[i]); if(!r||!r->data||r->cancelRequested||!selected.insert(coords[i]).second)continue;
        if(r->groupId && oldGroups.insert(r->groupId).second) {
            auto it=groups_.find(r->groupId);
            if(it!=groups_.end()) {
                coords.insert(coords.end(),it->second.members.begin(),it->second.members.end());
                edit|=it->second.edit;
                started=std::min(started,it->second.started);
            }
        }
    }
    if(selected.empty())return 0;
    for(auto id:oldGroups)groups_.erase(id);
    const auto id=nextGroup_++;
    Group g;g.started=started;g.edit=edit;
    for(auto c:selected) {
        auto r=find(c);r->version.fetch_add(1);r->groupId=id;
        r->pendingMesh.reset();r->uploaded=false;r->retries=0;r->state=ChunkState::Generated;
        r->dirty=static_cast<unsigned char>(ChunkDirty::Mesh);
        g.members.push_back(c);
    }
    groups_.emplace(id,std::move(g));
    return id;
}
void World::scheduleMeshing(const std::shared_ptr<ChunkRecord>& r,int priority) {
    if(!r->data||r->state!=ChunkState::Generated)return;
    ChunkNeighbors n;
    auto neighbors=adjacent(r->coord);
    std::shared_ptr<const ChunkData>* slots[4]{&n.west,&n.east,&n.north,&n.south};
    for(int i=0;i<4;++i) {
        auto other=find(neighbors[i]);
        if(!other||!other->data)return; // Generation radius supplies the border apron.
        *slots[i]=other->data;
    }
    if(!r->groupId)invalidateGroup({r->coord});
    if(!meshingCredits_->acquire()){++stats_.deferredMeshing;return;}
    if(!uploadCredits_->acquire()){meshingCredits_->release();++stats_.deferredMeshing;return;}
    auto lease=std::shared_ptr<void>(uploadCredits_.get(),[credits=uploadCredits_](void*){credits->release();});
    r->state=ChunkState::Meshing;
    {std::lock_guard lock(meshingMutex_);
     meshingQueue_.push({priority,r,r->data,n,r->version.load(),r->groupId,r->desiredLod,std::move(lease)});}
    meshingCv_.notify_one();
}
void World::generationLoop() {
    unsigned fairness=0;
    while(true) {
        std::array<GenerateJob,2> batch;std::size_t count=0;
        {std::unique_lock lock(generationMutex_);
         generationCv_.wait(lock,[&]{return stopping_||!generationQueue_.empty()||(heightmapEnabled_&&far_.hasJobs());});
         if(stopping_)return;
         if(generationQueue_.empty()||(++fairness%8==0&&heightmapEnabled_&&far_.hasJobs())){
             lock.unlock();for(unsigned n=0;n<4&&!stopping_;++n)if(!far_.work(generator_))break;
             meshingCv_.notify_all();continue;
         }
         while(count<batch.size()&&!generationQueue_.empty()){
             batch[count++]=generationQueue_.top();generationQueue_.pop();generationCredits_->release();
         }}
        for(std::size_t index=0;index<count&&!stopping_;++index){auto& j=batch[index];
        if(j.record->cancelRequested){j.record->generationState=GenerationState::Cancelled;std::lock_guard lock(statsMutex_);++stats_.cancelledJobs;continue;}
        j.record->state=ChunkState::Loading;
        j.record->generationState=GenerationState::Generating;
        std::shared_ptr<ChunkData> data;
        TraceScope trace(TracePhase::Generation,j.record->coord);
        auto start=Clock::now();
        try {data=generator_.generate(j.record->coord,j.revision,&j.record->cancelRequested);}
        catch(...) {j.record->generationState=GenerationState::Failed;std::lock_guard lock(statsMutex_);++stats_.failedJobs;}
        if(j.record->cancelRequested)j.record->generationState=GenerationState::Cancelled;
        {std::lock_guard lock(resultMutex_);generationResults_.push_back({j.record,data,j.instance,j.revision});}
        {std::lock_guard lock(statsMutex_);++stats_.generationJobs;if(j.record->cancelRequested)++stats_.cancelledJobs;stats_.generationCpuMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
        }
    }
}
void World::meshingLoop() {
    unsigned fairness=0;
    while(true) {
        MeshJob j;
        {std::unique_lock lock(meshingMutex_);
         meshingCv_.wait(lock,[&]{return stopping_||!meshingQueue_.empty()||(heightmapEnabled_&&far_.hasMeshJobs());});
         if(stopping_)return;
         if(meshingQueue_.empty()||(++fairness%8==0&&heightmapEnabled_&&far_.hasMeshJobs())){lock.unlock();far_.workMeshing(mesher_);generationCv_.notify_all();continue;}
         j=meshingQueue_.top();meshingQueue_.pop();meshingCredits_->release();}
        if(j.record->cancelRequested||j.record->version.load()!=j.revision){std::lock_guard lock(statsMutex_);++stats_.cancelledJobs;continue;}
        if(meshDelayMs_>0)std::this_thread::sleep_for(std::chrono::milliseconds(meshDelayMs_.load()));
        std::shared_ptr<ChunkMesh> mesh;
        TraceScope trace(TracePhase::Meshing,j.record->coord);
        auto start=Clock::now();
        try {
            if(failMeshes_.exchange(0)>0)throw std::runtime_error("Injected meshing failure");
            mesh=mesher_.build(j.record->coord,j.center,j.neighbors,j.lod);
            mesh->instanceId=j.record->instanceId;mesh->revision=j.revision;mesh->groupId=j.group;
            mesh->uploadLease=std::move(j.uploadLease);
        }catch(...) {std::lock_guard lock(statsMutex_);++stats_.failedJobs;}
        {std::lock_guard lock(resultMutex_);meshingResults_.push_back({j.record,mesh,j.revision});}
        {std::lock_guard lock(statsMutex_);++stats_.meshingJobs;stats_.meshingCpuMs+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
    }
}
void World::pumpResults() {
    std::vector<GenerateResult> generated;std::vector<MeshResult> meshed;
    {std::lock_guard lock(resultMutex_);generated.swap(generationResults_);meshed.swap(meshingResults_);}
    for(auto& result:generated) {
        auto r=result.record;
        if(r->cancelRequested||find(r->coord)!=r||r->instanceId!=result.instance||r->generationRevision!=result.revision){++stats_.staleResults;continue;}
        if(!result.data){
            r->generationState=GenerationState::Failed;r->state=ChunkState::Failed;
            if(++r->retries<=3){
                ++stats_.retriedJobs;
                ++r->generationRevision;r->generationState=GenerationState::Requested;r->state=ChunkState::Queued;
                std::lock_guard lock(generationMutex_);
                if(generationCredits_->acquire()){
                    generationQueue_.push({priorityFor(r->coord,playerChunk_,viewDirection_),r,r->instanceId,r->generationRevision,
                        std::chrono::duration<double>(Clock::now().time_since_epoch()).count()*100});generationCv_.notify_one();
                }else r->state=ChunkState::Unloaded;
            }
            continue;
        }
        r->retries=0;
        r->generationState=GenerationState::Ready;
        applyJournal(r->coord,*result.data);
        r->data=std::move(result.data);r->state=ChunkState::Generated;++stats_.generated;
        ++heightmapBoundaryRevision_;
        seedFluidChunk(r->coord);
        // Existing visible neighbors need a fresh border snapshot after arrival.
        std::vector<ChunkCoord> changed;
        for(auto c:adjacent(r->coord)){auto other=find(c);if(other&&other->activeMesh&&distance(c,playerChunk_)<=8)changed.push_back(c);}
        if(!changed.empty()) {changed.push_back(r->coord);invalidateGroup(changed);}
    }
    for(auto& result:meshed) {
        auto r=result.record;
        if(r->cancelRequested||find(r->coord)!=r||r->version.load()!=result.revision){++stats_.staleResults;continue;}
        if(!result.mesh){if(++r->retries<=3){r->state=ChunkState::Generated;++stats_.retriedJobs;}else r->state=ChunkState::Failed;continue;}
        r->pendingMesh=std::move(result.mesh);r->uploaded=false;r->state=ChunkState::Uploading;++stats_.meshed;
    }
}
void World::update(const std::array<float,3>& p,const std::array<float,3>& dir) {
    updateAt(WorldPosition::fromAbsolute(p[0],p[1],p[2]),dir);
}
void World::updateAt(const WorldPosition& position,const std::array<float,3>& dir){
    auto checked=position;checked.normalize();
    pumpResults();playerChunk_=checked.chunk;viewDirection_=dir;
    if(heightmapEnabled_)for(auto it=groups_.begin();it!=groups_.end();){
        auto& members=it->second.members;
        members.erase(std::remove_if(members.begin(),members.end(),[&](ChunkCoord c){
            if(distance(c,playerChunk_)<=8)return false;
            if(auto r=find(c)){r->groupId=0;r->pendingMesh.reset();r->uploaded=false;r->version.fetch_add(1);
                // The old in-flight mesh is now stale. Re-entry must be able to
                // queue a new mesh instead of remaining permanently Meshing/Uploading.
                if(r->data)r->state=ChunkState::Generated;
            }
            return true;
        }),members.end());
        if(members.empty())it=groups_.erase(it);else ++it;
    }
    std::vector<ChunkCoord> desired;
    for(int z=-10;z<=10;++z)for(int x=-10;x<=10;++x)desired.push_back({playerChunk_.x+x,playerChunk_.z+z});
    std::stable_sort(desired.begin(),desired.end(),[&](ChunkCoord a,ChunkCoord b){return priorityFor(a,playerChunk_,dir)<priorityFor(b,playerChunk_,dir);});
    for(auto c:desired)scheduleGeneration(c,priorityFor(c,playerChunk_,dir));
    auto publishedFar=heightmapEnabled_?far_.visible(playerChunk_):std::vector<std::shared_ptr<HeightmapStream::Tile>>{};
    for(auto it=chunks_.begin();it!=chunks_.end();) {
        auto r=it->second;
        if(heightmapEnabled_&&distance(r->coord,playerChunk_)>distanceBudget_.effective&&r->activeMesh){
            r->activeMesh.reset();if(!r->groupId&&r->data)r->state=ChunkState::Generated;
        }
        if(distance(it->first,playerChunk_)>12) {
            if(heightmapEnabled_&&r->activeMesh&&distance(it->first,playerChunk_)<=distanceBudget_.effective&&
                std::none_of(publishedFar.begin(),publishedFar.end(),[&](auto&t){return t->input.coord==it->first;})){
                // Retain only the old display mesh during an unfinished handoff.
                r->data.reset();++it;continue;
            }
            r->cancelRequested=true;r->state=ChunkState::Unloading;
            auto g=groups_.find(r->groupId);
            if(g!=groups_.end()){
                auto& m=g->second.members;
                m.erase(std::remove(m.begin(),m.end(),r->coord),m.end());
                if(m.empty())groups_.erase(g);
            }
            it=chunks_.erase(it);
        }else ++it;
    }
    std::vector<ChunkCoord> lodChanges;
    for(auto& [c,r]:chunks_)if(r->data) {
        auto lod=std::uint8_t{0};
        if(lod!=r->desiredLod){r->desiredLod=lod;if(r->activeMesh||r->groupId)lodChanges.push_back(c);}
    }
    if(!lodChanges.empty())invalidateGroup(lodChanges);
    std::vector<std::shared_ptr<ChunkRecord>> meshCandidates;
    for(auto&[c,r]:chunks_)if(r->data&&(distance(c,playerChunk_)<=8||r->groupId))meshCandidates.push_back(r);
    std::stable_sort(meshCandidates.begin(),meshCandidates.end(),[&](auto&a,auto&b){return priorityFor(a->coord,playerChunk_,dir)<priorityFor(b->coord,playerChunk_,dir);});
    for(auto&r:meshCandidates)scheduleMeshing(r,priorityFor(r->coord,playerChunk_,dir));
    if(heightmapEnabled_){
        auto resident=stats().cpuCacheBytes;
        auto allocated=resident+NearWorkerReservation;
        far_.setLimits(distanceBudget_.effective,32,64,allocated<limits_.cpuBytes?limits_.cpuBytes-allocated:0);
        far_.update(playerChunk_,[&](std::int64_t x,std::int64_t z)->std::optional<float>{
            auto r=find(coordOf(x,z));if(!r||!r->data||distance(r->coord,playerChunk_)>8)return {};
            for(int y=255;y>=0;--y)if(r->data->get(blockLocal(x),y,blockLocal(z))!=BlockType::Air)return float(y+1);
            return 0.0f;
        },dir,[&](ChunkCoord c){auto r=find(c);return r&&bool(r->activeMesh);},heightmapBoundaryRevision_,
          [&](ChunkCoord c){return captureFarSnapshot(c);});
        generationCv_.notify_all();
    }
    stats_.loaded=chunks_.size();
    stats_.generationQueue=generationCredits_->used.load();
    stats_.meshingQueue=meshingCredits_->used.load();
}
std::vector<ChunkUpload> World::takePendingUploads() {
    std::vector<ChunkUpload> out;
    for(auto& [c,r]:chunks_)if(r->pendingMesh&&!r->uploaded&&r->state==ChunkState::Uploading)out.push_back({c,r->pendingMesh});
    if(heightmapEnabled_)for(auto&t:far_.pending())out.push_back({t->input.coord,t->pending});
    std::sort(out.begin(),out.end(),[&](auto&a,auto&b){return priorityFor(a.coord,playerChunk_,viewDirection_)<priorityFor(b.coord,playerChunk_,viewDirection_);});
    if(out.size()>limits_.uploadQueue)out.resize(limits_.uploadQueue);
    return out;
}
bool World::markUploaded(const ChunkUpload& u) {
    if(u.mesh&&(u.mesh->heightmap||u.mesh->surface3D)){for(auto&t:far_.pending())if(t->pending==u.mesh){t->uploaded=true;u.mesh->uploadLease.reset();meshingCv_.notify_all();return true;}return false;}
    auto r=find(u.coord);
    if(!r||!u.mesh||r->pendingMesh!=u.mesh||u.mesh->instanceId!=r->instanceId||u.mesh->revision!=r->version)return false;
    r->uploaded=true;u.mesh->uploadLease.reset();meshingCv_.notify_all();return true;
}
std::vector<UploadGroup> World::readyGroups()const {
    std::vector<UploadGroup> out;
    for(auto& [id,g]:groups_) {
        UploadGroup result;result.id=id;bool ready=true;
        for(auto c:g.members) {
            auto r=find(c);
            if(!r||!r->pendingMesh||!r->uploaded||r->groupId!=id||r->pendingMesh->revision!=r->version){ready=false;break;}
            result.members.push_back({c,r->pendingMesh});
        }
        if(ready&&!result.members.empty())out.push_back(std::move(result));
    }
    if(heightmapEnabled_){
        if(far_.adaptive()){
            for(auto& batch:far_.readyBatches()){
                UploadGroup group;group.id=batch.front()->group;
                for(auto&t:batch)group.members.push_back({t->input.coord,t->pending});
                out.push_back(std::move(group));
            }
            return out;
        }
        auto farReady=far_.ready();auto farActive=far_.visible(playerChunk_);
        auto dependent=[&](const Group& group){
            for(auto c:group.members)if(distance(c,playerChunk_)>=8||
                std::any_of(farActive.begin(),farActive.end(),[&](auto&t){return t->input.coord==c;}))return true;
            return false;
        };
        if(!far_.settled()){
            bool allReady=!farReady.empty();
            for(auto&[id,group]:groups_)if(dependent(group)&&
                std::none_of(out.begin(),out.end(),[&](const UploadGroup& g){return g.id==id;}))allReady=false;
            UploadGroup combined;
            if(allReady){
                combined.id=farReady.front()->group|(1ULL<<62);
                combined.linkedGroups.push_back(farReady.front()->group);
                for(auto&t:farReady)combined.members.push_back({t->input.coord,t->pending});
            }
            for(auto it=out.begin();it!=out.end();)if(dependent(groups_.at(it->id))){
                if(allReady){combined.linkedGroups.push_back(it->id);combined.members.insert(combined.members.end(),it->members.begin(),it->members.end());}
                it=out.erase(it);
            }else ++it;
            if(allReady)out.push_back(std::move(combined));
        }
    }
    return out;
}
bool World::commitGroup(const UploadGroup& g) {
    if(!g.linkedGroups.empty()){
        // Validate the transaction before changing any CPU publication state.
        auto candidates=readyGroups();
        if(std::none_of(candidates.begin(),candidates.end(),[&](const UploadGroup& ready){return ready.id==g.id&&ready.linkedGroups==g.linkedGroups&&ready.members.size()==g.members.size();}))return false;
        for(auto id:g.linkedGroups){
            UploadGroup sub;sub.id=id;
            for(auto&u:g.members)if(u.mesh->groupId==id)sub.members.push_back(u);
            if(!commitGroup(sub))throw std::logic_error("validated publication transaction changed");
        }
        return true;
    }
    if(g.id&(1ULL<<63)){
        std::vector<std::shared_ptr<HeightmapStream::Tile>> tiles;
        if(far_.adaptive()){
            for(auto& batch:far_.readyBatches())if(batch.front()->group==g.id){tiles=std::move(batch);break;}
        }else tiles=far_.ready();
        if(tiles.size()!=g.members.size())return false;
        for(auto&u:g.members)if(std::none_of(tiles.begin(),tiles.end(),[&](auto&t){return t->pending==u.mesh;}))return false;
        if(!far_.commit(tiles))return false;
        // GPU ownership at this coordinate has changed to the far representation.
        // Do not later select a cached voxel CPU mesh whose GPU buffers were retired.
        for(auto&t:tiles)if(auto r=find(t->input.coord)){
            if(distance(r->coord,playerChunk_)>8){r->activeMesh.reset();if(!r->groupId&&r->data)r->state=ChunkState::Generated;}
        }
        return true;}
    auto it=groups_.find(g.id);if(it==groups_.end()||it->second.members.size()!=g.members.size())return false;
    for(auto& u:g.members){auto r=find(u.coord);if(!r||!r->uploaded||r->groupId!=g.id||r->pendingMesh!=u.mesh||r->version!=u.mesh->revision)return false;}
    for(auto& u:g.members){auto r=find(u.coord);r->activeMesh=r->pendingMesh;r->pendingMesh.reset();r->groupId=0;r->state=ChunkState::Ready;r->dirty=0;
        if(heightmapEnabled_)far_.voxelPublished(u.coord);}
    if(it->second.edit)stats_.lastPublishMs=std::chrono::duration<double,std::milli>(Clock::now()-it->second.started).count();
    ++stats_.swaps;groups_.erase(it);return true;
}
std::vector<RenderChunk> World::renderChunks(const std::array<float,3>& p,const std::array<float,3>& dir)const {
    return renderChunksAt(WorldPosition::fromAbsolute(p[0],p[1],p[2]),dir);
}
std::vector<RenderChunk> World::renderChunksAt(const WorldPosition& p,const std::array<float,3>& dir)const {
    auto checked=p;checked.normalize();std::vector<RenderChunk> out;auto center=checked.chunk;
    auto farVisible=heightmapEnabled_?far_.visible(center):std::vector<std::shared_ptr<HeightmapStream::Tile>>{};
    for(auto& [c,r]:chunks_) {
        if(!r->activeMesh||r->cancelRequested)continue;
        if(distance(c,center)>8){
            if(!heightmapEnabled_||distance(c,center)>distanceBudget_.effective||
                std::any_of(farVisible.begin(),farVisible.end(),[&](auto&t){return t->input.coord==c;}))continue;
        }
        float dx=static_cast<float>(c.x-center.x),dz=static_cast<float>(c.z-center.z),len=std::sqrt(dx*dx+dz*dz);
        if(len>1&&(dir[0]*dx+dir[2]*dz)/len<-0.92f)continue;
        out.push_back({c,r->activeMesh});
    }
    if(heightmapEnabled_)for(auto&t:farVisible){
        if(distance(t->input.coord,center)>distanceBudget_.effective)continue;
        auto r=find(t->input.coord);if(distance(t->input.coord,center)<=8&&r&&r->activeMesh)continue;
        out.push_back({t->input.coord,t->active,far_.morph(*t)});
    }
    stats_.visible=out.size();return out;
}
std::vector<ChunkCoord> World::loadedCoords()const {auto out=far_.coords();for(auto& [c,r]:chunks_)out.push_back(c);return out;}
std::vector<ChunkCoord> World::gpuCoords()const{
    auto out=far_.coords();for(auto&[c,r]:chunks_)if(r->activeMesh||r->pendingMesh||distance(c,playerChunk_)<=8)out.push_back(c);
    return out;
}
std::optional<ChunkInspection> World::inspectChunk(ChunkCoord coord)const{
    auto r=find(coord);if(!r)return {};
    return ChunkInspection{r->state.load(),r->generationState.load(),r->instanceId,r->version.load(),r->generationRevision,r->groupId,
        r->activeMesh?r->activeMesh->revision:0,r->pendingMesh?r->pendingMesh->revision:0,
        bool(r->data),bool(r->activeMesh),bool(r->pendingMesh),r->uploaded};
}
BlockType World::get(std::int64_t x,int y,std::int64_t z)const {auto r=find(coordOf(x,z));return r&&r->data?r->data->get(blockLocal(x),y,blockLocal(z)):BlockType::Air;}
BlockState World::getState(std::int64_t x,int y,std::int64_t z)const{auto r=find(coordOf(x,z));return r&&r->data?r->data->getState(blockLocal(x),y,blockLocal(z)):BlockState{};}
bool World::columnLoaded(std::int64_t x,std::int64_t z)const {
    if(x<-WorldLimit||x>=WorldLimit||z<-WorldLimit||z>=WorldLimit)return false;
    auto r=find(coordOf(x,z));return r&&r->data&&!r->cancelRequested;
}
bool World::targetable(std::int64_t x,std::int64_t z)const {auto r=find(coordOf(x,z));return columnLoaded(x,z)&&r&&r->activeMesh&&r->groupId==0&&r->activeMesh->revision==r->version&&!r->cancelRequested;}
bool World::set(std::int64_t x,int y,std::int64_t z,BlockType b) {
    return setState(x,y,z,BlockState(b));
}
bool World::setState(std::int64_t x,int y,std::int64_t z,BlockState b){
    auto c=coordOf(x,z);auto r=find(c);
    if(!columnLoaded(x,z)||!r||!r->data||y<=0||y>=256||get(x,y,z)==BlockType::Bedrock||getState(x,y,z)==b||b.type()>=BlockType::Count)return false;
    auto data=std::make_shared<ChunkData>(*r->data);
    int lx=blockLocal(x),lz=blockLocal(z);
    if(!data->setState(lx,y,lz,b))return false;
    r->data=data;r->desiredLod=0;
    ++heightmapBoundaryRevision_;
    edits_[c][lx|(y<<4)|(lz<<12)]=b;
    dirtyEditSnapshots_.insert(c);
    fluids_.notify({x,y,z});
    std::vector<ChunkCoord> affected{c};
    if(lx==0)affected.push_back({c.x-1,c.z});
    if(lx==15)affected.push_back({c.x+1,c.z});
    if(lz==0)affected.push_back({c.x,c.z-1});
    if(lz==15)affected.push_back({c.x,c.z+1});
    invalidateGroup(affected,true);return true;
}
void World::seedFluidChunk(ChunkCoord c){
    if(generator_.settings().generatorVersion!="density-world-1"||distance(c,playerChunk_)>6)return;
    auto r=find(c);if(!r||!r->data)return;
    for(int y=1;y<256;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x){
        auto state=r->data->getState(x,y,z);if(state!=BlockType::Water)continue;
        Int3 p{c.x*16+x,y,c.z*16+z};bool unstable=!state.source();
        constexpr int delta[5][3]{{0,-1,0},{-1,0,0},{1,0,0},{0,0,-1},{0,0,1}};
        for(auto&d:delta)if(columnLoaded(p.x+d[0],p.z+d[2])){
            auto neighbor=get(p.x+d[0],p.y+d[1],p.z+d[2]);
            unstable|=neighbor==BlockType::Air||isFlower(neighbor);
        }else unstable=true; // Keep a frozen frontier at unloaded borders until data arrives.
        if(unstable)fluids_.notify(p);
    }
}
void World::applyFluid(const std::vector<FluidUpdate>& changes){
    std::unordered_map<ChunkCoord,std::shared_ptr<ChunkData>,ChunkCoordHash> copies;
    std::vector<ChunkCoord> affected;
    for(auto&u:changes){
        auto c=coordOf(u.position.x,u.position.z);auto r=find(c);
        if(!r||!r->data||getState(u.position.x,u.position.y,u.position.z)!=u.before||u.position.y<=0||u.position.y>=256)continue;
        auto& copy=copies[c];if(!copy)copy=std::make_shared<ChunkData>(*r->data);
        int x=blockLocal(u.position.x),z=blockLocal(u.position.z),y=u.position.y;
        if(!copy->setState(x,y,z,u.after))continue;
        edits_[c][x|(y<<4)|(z<<12)]=u.after;affected.push_back(c);
        dirtyEditSnapshots_.insert(c);
        if(x==0)affected.push_back({c.x-1,c.z});
        if(x==15)affected.push_back({c.x+1,c.z});
        if(z==0)affected.push_back({c.x,c.z-1});
        if(z==15)affected.push_back({c.x,c.z+1});
    }
    for(auto&[c,data]:copies)find(c)->data=std::move(data);
    if(!copies.empty()){++heightmapBoundaryRevision_;invalidateGroup(affected);}
}
void World::advanceSimulation(double dt){
    if(generator_.settings().generatorVersion!="density-world-1")return;
    fluids_.setRegion(playerChunk_);
    std::unordered_set<ChunkCoord,ChunkCoordHash> region;
    for(auto&[c,r]:chunks_)if(r->data&&distance(c,playerChunk_)<=6){
        region.insert(c);if(!fluidRegion_.count(c)){
            seedFluidChunk(c);for(auto n:adjacent(c))seedFluidChunk(n);
        }
    }
    fluidRegion_=std::move(region);
    fluids_.advance(dt,[&](const Int3&p){return FluidCell{getState(p.x,p.y,p.z),columnLoaded(p.x,p.z)};},
        [&](const std::vector<FluidUpdate>& changes){applyFluid(changes);});
}
void World::setResourceLimits(const ResourceLimits& limits){limits.validate();limits_=limits;distanceBudget_.effective=std::min(distanceBudget_.effective,limits.requestedFarRadius);generationCredits_->capacity=limits.generationQueue;meshingCredits_->capacity=limits.meshingQueue;uploadCredits_->capacity=limits.uploadQueue;}
void World::reportGpuPressure(std::size_t bytes,double dt){
    auto stats=World::stats();auto cpu=far_.memoryPressure()?limits_.cpuBytes:stats.cpuCacheBytes+NearWorkerReservation;
    // Recovery must afford a new ring AND one 128 MiB far-job reservation.
    // Otherwise the controller can alternate between admitting and immediately
    // cancelling the same outer band every two seconds without ever settling.
    std::size_t growth=far_.tileCount()?stats.cpuCacheBytes/far_.tileCount()*std::size_t(8*(distanceBudget_.effective+1)):16*MiB;
    bool growthAllowed=cpu<limits_.cpuBytes&&128*MiB<limits_.cpuBytes-cpu&&growth<limits_.cpuBytes-cpu-128*MiB;
    distanceReason_=bytes>limits_.gpuBytes*9/10?"GPU mesh budget":cpu>limits_.cpuBytes*9/10?"CPU cache/job headroom":
        distanceBudget_.effective<limits_.requestedFarRadius?"waiting for two-second headroom":"requested distance";
    if(!growthAllowed&&cpu<=limits_.cpuBytes*9/10&&bytes<=limits_.gpuBytes*9/10&&distanceBudget_.effective<limits_.requestedFarRadius)distanceReason_="next-ring/job reservation headroom";
    distanceBudget_.update(dt,cpu,bytes,limits_,growthAllowed);
}
FarVoxelSnapshot World::captureFarSnapshot(ChunkCoord coord){
    FarVoxelSnapshot snapshot;
    std::array<ChunkCoord,5> positions{{coord,{coord.x-1,coord.z},{coord.x+1,coord.z},{coord.x,coord.z-1},{coord.x,coord.z+1}}};
    std::shared_ptr<const ChunkData>* data[]{&snapshot.center,&snapshot.neighbors.west,&snapshot.neighbors.east,&snapshot.neighbors.north,&snapshot.neighbors.south};
    for(unsigned i=0;i<5;++i){
        auto c=positions[i];if(auto record=find(c))if(record->data&&!record->cancelRequested)*data[i]=record->data;
        auto edits=edits_.find(c);if(edits==edits_.end())continue;
        auto cached=editSnapshots_.find(c);bool dirty=dirtyEditSnapshots_.erase(c)!=0;
        if(cached==editSnapshots_.end()||dirty){editSnapshots_[c]=std::make_shared<const ChunkEdits>(edits->second);}
        snapshot.edits[i]=editSnapshots_.at(c);
    }
    return snapshot;
}
BlockTarget World::target(const std::array<float,3>& o,const std::array<float,3>& d,float reach)const {
    return targetAt(WorldPosition::fromAbsolute(o[0],o[1],o[2]),d,reach);
}
BlockTarget World::targetAt(const WorldPosition& o,const std::array<float,3>& d,float reach)const {
    return traceWorldVoxels(o,d,reach,[&](std::int64_t x,int y,std::int64_t z){
        auto r=find(coordOf(x,z));
        if(!targetable(x,z))return TargetVoxel{BlockType::Air,true,0,0};
        return TargetVoxel{get(x,y,z),false,r->instanceId,r->version};
    });
}
bool World::validateTarget(const BlockTarget& old,const std::array<float,3>& o,const std::array<float,3>& d)const {
    return validateTargetAt(old,WorldPosition::fromAbsolute(o[0],o[1],o[2]),d);
}
bool World::validateTargetAt(const BlockTarget& old,const WorldPosition& o,const std::array<float,3>& d)const {
    if(!old.hit)return false;
    auto now=targetAt(o,d,6);
    return now.hit&&now.block==old.block&&now.normal==old.normal&&now.type==old.type&&now.instanceId==old.instanceId&&now.revision==old.revision;
}
RaycastHit World::raycast(const std::array<float,3>& o,const std::array<float,3>& d,float reach)const {
    return traceVoxels(o,d,reach,[&](std::int64_t x,int y,std::int64_t z){return TargetVoxel{get(x,y,z),!columnLoaded(x,z),0,0};});
}
WorldStats World::stats()const {
    std::lock_guard lock(statsMutex_);auto s=stats_;s.heightmapCpuMs=far_.buildMs;s.heightmapJobs=far_.completed;s.heightmapStale=far_.stale;s.farCancelled=far_.cancelled;
    s.cpuReservations=far_.reservedBytes()+NearWorkerReservation;
    s.uploadStaging=uploadCredits_->used.load();
    s.farPendingUploads=far_.pending().size();s.farGroupReady=far_.ready().size();s.publicationGroups=groups_.size();
    s.ready=0;s.vertices=0;s.indices=0;
    thread_local std::vector<const ChunkData*> countedData;countedData.clear();
    {std::lock_guard resultsLock(resultMutex_);
        for(auto&result:generationResults_)if(result.data){s.cpuCacheBytes+=sizeof(ChunkData);countedData.push_back(result.data.get());}
        for(auto&result:meshingResults_)if(result.mesh)s.cpuCacheBytes+=result.mesh->residentByteSize();
    }
    for(auto&[c,r]:chunks_){
        if(r->data){s.cpuCacheBytes+=sizeof(ChunkData);
            countedData.push_back(r->data.get());
            for(unsigned i=0;i<s.blockCounts.size();++i)s.blockCounts[i]+=r->data->materialCounts[i];
            for(unsigned i=0;i<16;++i)s.fluidCounts[i]+=r->data->fluidCounts[i];
            s.treeIncidences+=r->data->treeIncidences;s.villageIncidences+=r->data->villageIncidences;
        }
        if(r->pendingMesh)s.cpuCacheBytes+=r->pendingMesh->residentByteSize();
        if(r->pendingMesh&&!r->uploaded)++s.pendingUploads;
        if(r->activeMesh){if(distance(c,playerChunk_)<=8)++s.ready;s.cpuCacheBytes+=r->activeMesh->residentByteSize();
            auto& mesh=*r->activeMesh;s.vertices+=mesh.vertices.size()+mesh.cutoutVertices.size()+mesh.plantVertices.size()+mesh.translucentVertices.size()+mesh.heightVertices.size();
            s.indices+=mesh.indices.size()+mesh.cutoutIndices.size()+mesh.plantIndices.size()+mesh.translucentIndices.size();}
    }
    for(auto&t:far_.visible(playerChunk_))if(t->active){
        auto&mesh=*t->active;
        s.vertices+=mesh.surfaceVertices.size()+mesh.plantVertices.size()+mesh.translucentVertices.size();
        s.indices+=mesh.coarseIndices.size()+mesh.plantIndices.size()+mesh.translucentIndices.size();
        s.collapses+=mesh.collapseAccepted;s.rejectedCollapses+=mesh.collapseRejected;s.refinedVertices+=mesh.refinedVertices;
        ++s.lodTiles[mesh.lod==2?0:mesh.lod==4?1:2];s.morphingTiles+=far_.morph(*t)<1;
    }
    std::sort(countedData.begin(),countedData.end(),std::less<const ChunkData*>{});
    s.cpuCacheBytes+=far_.residentBytes(countedData);
    for(auto&[c,edits]:edits_)s.journalPayloadBytes+=edits.size()*(sizeof(std::uint32_t)+sizeof(BlockState));
    return s;
}
}
