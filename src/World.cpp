#include <bloxide/World.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <stdexcept>
namespace bloxide {
namespace {
ChunkCoord coordOf(int x,int z) {
    auto div=[](int v){return v/16-(v%16<0);};
    return {div(x),div(z)};
}
int distance(ChunkCoord a,ChunkCoord b) {
    return static_cast<int>(std::min<std::int64_t>(1000000,std::max(std::llabs(a.x-b.x),std::llabs(a.z-b.z))));
}
std::array<ChunkCoord,4> adjacent(ChunkCoord c) { return {{{c.x-1,c.z},{c.x+1,c.z},{c.x,c.z-1},{c.x,c.z+1}}}; }
}
World::World() {
    for(int i=0;i<2;++i) generationThreads_.emplace_back(&World::generationLoop,this);
    for(int i=0;i<2;++i) meshingThreads_.emplace_back(&World::meshingLoop,this);
}
World::~World() {
    stopping_=true; generationCv_.notify_all(); meshingCv_.notify_all();
    for(auto& t:generationThreads_) t.join();
    for(auto& t:meshingThreads_) t.join();
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
    if(find(c)) return;
    auto r=std::make_shared<ChunkRecord>(c);
    r->instanceId=nextInstance_++; r->version=1; r->state=ChunkState::Queued;
    chunks_.emplace(c,r);
    {std::lock_guard lock(generationMutex_);generationQueue_.push({priority,r});}
    generationCv_.notify_one();
}
void World::applyJournal(ChunkCoord c,ChunkData& data) {
    auto it=edits_.find(c); if(it==edits_.end())return;
    for(auto [key,value]:it->second) data.set(key&15U,(key>>4U)&255U,(key>>12U)&15U,value);
}
std::uint64_t World::invalidateGroup(std::vector<ChunkCoord> coords,bool edit) {
    std::unordered_set<ChunkCoord,ChunkCoordHash> selected;
    std::unordered_set<std::uint64_t> oldGroups;
    auto started=Clock::now();
    for(std::size_t i=0;i<coords.size();++i) {
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
    r->state=ChunkState::Meshing;
    {std::lock_guard lock(meshingMutex_);
     meshingQueue_.push({priority,r,r->data,n,r->version.load(),r->groupId,r->desiredLod});}
    meshingCv_.notify_one();
}
void World::generationLoop() {
    while(true) {
        GenerateJob j;
        {std::unique_lock lock(generationMutex_);
         generationCv_.wait(lock,[&]{return stopping_||!generationQueue_.empty();});
         if(stopping_)return;
         j=generationQueue_.top();generationQueue_.pop();}
        if(j.record->cancelRequested)continue;
        std::shared_ptr<ChunkData> data;
        try {data=generator_.generate(j.record->coord);}
        catch(...) {std::lock_guard lock(statsMutex_);++stats_.failedJobs;}
        {std::lock_guard lock(resultMutex_);generationResults_.push_back({j.record,data});}
        {std::lock_guard lock(statsMutex_);++stats_.generationJobs;}
    }
}
void World::meshingLoop() {
    while(true) {
        MeshJob j;
        {std::unique_lock lock(meshingMutex_);
         meshingCv_.wait(lock,[&]{return stopping_||!meshingQueue_.empty();});
         if(stopping_)return;
         j=meshingQueue_.top();meshingQueue_.pop();}
        if(j.record->cancelRequested||j.record->version.load()!=j.revision)continue;
        if(meshDelayMs_>0)std::this_thread::sleep_for(std::chrono::milliseconds(meshDelayMs_.load()));
        std::shared_ptr<ChunkMesh> mesh;
        try {
            if(failMeshes_.exchange(0)>0)throw std::runtime_error("Injected meshing failure");
            mesh=mesher_.build(j.record->coord,j.center,j.neighbors,j.lod);
            mesh->instanceId=j.record->instanceId;mesh->revision=j.revision;mesh->groupId=j.group;
        }catch(...) {std::lock_guard lock(statsMutex_);++stats_.failedJobs;}
        {std::lock_guard lock(resultMutex_);meshingResults_.push_back({j.record,mesh,j.revision});}
        {std::lock_guard lock(statsMutex_);++stats_.meshingJobs;}
    }
}
void World::pumpResults() {
    std::vector<GenerateResult> generated;std::vector<MeshResult> meshed;
    {std::lock_guard lock(resultMutex_);generated.swap(generationResults_);meshed.swap(meshingResults_);}
    for(auto& result:generated) {
        auto r=result.record;
        if(r->cancelRequested||find(r->coord)!=r){++stats_.staleResults;continue;}
        if(!result.data){r->state=ChunkState::Failed;continue;}
        applyJournal(r->coord,*result.data);
        r->data=std::move(result.data);r->state=ChunkState::Generated;++stats_.generated;
        // Existing visible neighbors need a fresh border snapshot after arrival.
        std::vector<ChunkCoord> changed;
        for(auto c:adjacent(r->coord)){auto other=find(c);if(other&&other->activeMesh&&distance(c,playerChunk_)<=8)changed.push_back(c);}
        if(!changed.empty()) {changed.push_back(r->coord);invalidateGroup(changed);}
    }
    for(auto& result:meshed) {
        auto r=result.record;
        if(r->cancelRequested||find(r->coord)!=r||r->version.load()!=result.revision){++stats_.staleResults;continue;}
        if(!result.mesh){r->state=++r->retries<=3?ChunkState::Generated:ChunkState::Failed;continue;}
        r->pendingMesh=std::move(result.mesh);r->uploaded=false;r->state=ChunkState::Uploading;++stats_.meshed;
    }
}
void World::update(const std::array<float,3>& p,const std::array<float,3>& dir) {
    pumpResults();playerChunk_=worldToChunk(p[0],p[2]);viewDirection_=dir;
    for(int z=-10;z<=10;++z)for(int x=-10;x<=10;++x) {
        ChunkCoord c{playerChunk_.x+x,playerChunk_.z+z};
        scheduleGeneration(c,priorityFor(c,playerChunk_,dir));
    }
    for(auto it=chunks_.begin();it!=chunks_.end();) {
        auto r=it->second;
        if(distance(it->first,playerChunk_)>12) {
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
        auto lod=static_cast<std::uint8_t>(!r->data->edited&&distance(c,playerChunk_)>4);
        if(lod!=r->desiredLod){r->desiredLod=lod;if(r->activeMesh||r->groupId)lodChanges.push_back(c);}
    }
    if(!lodChanges.empty())invalidateGroup(lodChanges);
    for(auto& [c,r]:chunks_)
        if(r->data&&(distance(c,playerChunk_)<=8||r->groupId))scheduleMeshing(r,priorityFor(c,playerChunk_,dir));
    stats_.loaded=chunks_.size();
    {std::lock_guard lock(generationMutex_);stats_.generationQueue=generationQueue_.size();}
    {std::lock_guard lock(meshingMutex_);stats_.meshingQueue=meshingQueue_.size();}
}
std::vector<ChunkUpload> World::takePendingUploads() {
    std::vector<ChunkUpload> out;
    for(auto& [c,r]:chunks_)if(r->pendingMesh&&!r->uploaded&&r->state==ChunkState::Uploading)out.push_back({c,r->pendingMesh});
    std::sort(out.begin(),out.end(),[&](auto&a,auto&b){return priorityFor(a.coord,playerChunk_,viewDirection_)<priorityFor(b.coord,playerChunk_,viewDirection_);});
    return out;
}
bool World::markUploaded(const ChunkUpload& u) {
    auto r=find(u.coord);
    if(!r||!u.mesh||r->pendingMesh!=u.mesh||u.mesh->instanceId!=r->instanceId||u.mesh->revision!=r->version)return false;
    r->uploaded=true;return true;
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
    return out;
}
bool World::commitGroup(const UploadGroup& g) {
    auto it=groups_.find(g.id);if(it==groups_.end()||it->second.members.size()!=g.members.size())return false;
    for(auto& u:g.members){auto r=find(u.coord);if(!r||!r->uploaded||r->groupId!=g.id||r->pendingMesh!=u.mesh||r->version!=u.mesh->revision)return false;}
    for(auto& u:g.members){auto r=find(u.coord);r->activeMesh=r->pendingMesh;r->pendingMesh.reset();r->groupId=0;r->state=ChunkState::Ready;r->dirty=0;}
    if(it->second.edit)stats_.lastPublishMs=std::chrono::duration<double,std::milli>(Clock::now()-it->second.started).count();
    ++stats_.swaps;groups_.erase(it);return true;
}
std::vector<RenderChunk> World::renderChunks(const std::array<float,3>& p,const std::array<float,3>& dir)const {
    std::vector<RenderChunk> out;auto center=worldToChunk(p[0],p[2]);
    for(auto& [c,r]:chunks_) {
        if(!r->activeMesh||r->cancelRequested||distance(c,center)>8)continue;
        float dx=static_cast<float>(c.x-center.x),dz=static_cast<float>(c.z-center.z),len=std::sqrt(dx*dx+dz*dz);
        if(len>1&&(dir[0]*dx+dir[2]*dz)/len<-0.92f)continue;
        out.push_back({c,r->activeMesh});
    }
    stats_.visible=out.size();return out;
}
std::vector<ChunkCoord> World::loadedCoords()const {std::vector<ChunkCoord> out;for(auto& [c,r]:chunks_)out.push_back(c);return out;}
BlockType World::get(int x,int y,int z)const {auto r=find(coordOf(x,z));return r&&r->data?r->data->get(floorMod(x,16),y,floorMod(z,16)):BlockType::Air;}
bool World::columnLoaded(int x,int z)const {auto r=find(coordOf(x,z));return r&&r->data&&!r->cancelRequested;}
bool World::targetable(int x,int z)const {auto r=find(coordOf(x,z));return r&&r->data&&r->activeMesh&&r->groupId==0&&r->activeMesh->revision==r->version&&!r->cancelRequested;}
bool World::set(int x,int y,int z,BlockType b) {
    auto c=coordOf(x,z);auto r=find(c);
    if(!r||!r->data||y<=0||y>=256||get(x,y,z)==BlockType::Bedrock||get(x,y,z)==b)return false;
    auto data=std::make_shared<ChunkData>(*r->data);
    int lx=floorMod(x,16),lz=floorMod(z,16);
    if(!data->set(lx,y,lz,b))return false;
    r->data=data;r->desiredLod=0;
    edits_[c][lx|(y<<4)|(lz<<12)]=b;
    std::vector<ChunkCoord> affected{c};
    if(lx==0)affected.push_back({c.x-1,c.z});
    if(lx==15)affected.push_back({c.x+1,c.z});
    if(lz==0)affected.push_back({c.x,c.z-1});
    if(lz==15)affected.push_back({c.x,c.z+1});
    invalidateGroup(affected,true);return true;
}
BlockTarget World::target(const std::array<float,3>& o,const std::array<float,3>& d,float reach)const {
    return traceVoxels(o,d,reach,[&](int x,int y,int z){
        auto r=find(coordOf(x,z));
        if(!targetable(x,z))return TargetVoxel{BlockType::Air,true,0,0};
        return TargetVoxel{get(x,y,z),false,r->instanceId,r->version};
    });
}
bool World::validateTarget(const BlockTarget& old,const std::array<float,3>& o,const std::array<float,3>& d)const {
    if(!old.hit)return false;
    auto now=target(o,d,6);
    return now.hit&&now.block==old.block&&now.normal==old.normal&&now.type==old.type&&now.instanceId==old.instanceId&&now.revision==old.revision;
}
RaycastHit World::raycast(const std::array<float,3>& o,const std::array<float,3>& d,float reach)const {
    return traceVoxels(o,d,reach,[&](int x,int y,int z){return TargetVoxel{get(x,y,z),!columnLoaded(x,z),0,0};});
}
WorldStats World::stats()const {std::lock_guard lock(statsMutex_);auto s=stats_;s.ready=0;s.vertices=0;s.indices=0;for(auto&[c,r]:chunks_)if(r->activeMesh){++s.ready;s.vertices+=r->activeMesh->vertices.size();s.indices+=r->activeMesh->indices.size();}return s;}
}
