#include <bloxide/Heightmap.hpp>
#include <bloxide/Coordinates.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace bloxide {
namespace {int radius(ChunkCoord a,ChunkCoord b){return int(std::max(std::llabs(a.x-b.x),std::llabs(a.z-b.z)));}}
int HeightmapMesher::selectStep(int r,int previous){
    if(previous==2&&r<=13)return 2;
    if(previous==4&&r>=12&&r<=19)return 4;
    if(previous==8&&r>=18)return 8;
    return r<=12?2:r<=18?4:8;
}
float HeightmapMesher::heightOnMesh(const ChunkMesh& m,float x,float z,float morph){
    for(std::size_t i=0;i+2<m.indices.size();i+=3){
        auto a=m.heightVertices[m.indices[i]],b=m.heightVertices[m.indices[i+1]],c=m.heightVertices[m.indices[i+2]];
        if(a.face!=2||b.face!=2||c.face!=2)continue;
        double d=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(d)<1e-9)continue;
        double u=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/d;
        double v=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/d,w=1-u-v;
        if(u>=-1e-5&&v>=-1e-5&&w>=-1e-5)return float(u*(a.fromY+(a.y-a.fromY)*morph)+v*(b.fromY+(b.y-b.fromY)*morph)+w*(c.fromY+(c.y-c.fromY)*morph));
    }
    return std::numeric_limits<float>::quiet_NaN();
}
std::shared_ptr<ChunkMesh> HeightmapMesher::build(const ChunkGenerator& gen,const HeightTileInput& in,const std::atomic<bool>* cancel){
    if(in.step!=2&&in.step!=4&&in.step!=8)throw std::invalid_argument("heightmap step must be 2,4,8");
    if(in.coord.x<-WorldLimit/16||in.coord.x>=WorldLimit/16||in.coord.z<-WorldLimit/16||in.coord.z>=WorldLimit/16)
        throw std::out_of_range("heightmap coordinate outside gameplay bounds");
    for(int edge=0;edge<4;++edge)if(in.voxelEdge[edge])for(float h:in.edges[edge])
        if(!std::isfinite(h)||h<0||h>256)throw std::invalid_argument("invalid voxel boundary snapshot");
    auto mesh=std::make_shared<ChunkMesh>();mesh->coord=in.coord;mesh->heightmap=true;mesh->lod=std::uint8_t(in.step);
    const auto bx=in.coord.x*16,bz=in.coord.z*16;
    auto height=[&](int x,int z){
        float h=float(gen.sampleTerrain(bx+x,bz+z,(x==0||z==0||x==16||z==16)?1:in.step).height+1);
        if((x==0||x==16)&&(z==0||z==16))return h; // canonical shared corner
        if(x==0&&in.voxelEdge[0])h=in.edges[0][z];
        if(x==16&&in.voxelEdge[1])h=in.edges[1][z];
        if(z==0&&in.voxelEdge[2])h=in.edges[2][x];
        if(z==16&&in.voxelEdge[3])h=in.edges[3][x];
        return h;
    };
    auto vertex=[&](int x,int z){
        float h=height(x,z),old=h;
        // Fixed, full-resolution shared edges: morph only the filtered interior.
        if(in.previous&&x>0&&x<16&&z>0&&z<16){
            float p=heightOnMesh(*in.previous,float(x),float(z),in.previousMorph);if(std::isfinite(p))old=p;
        }
        auto index=std::uint32_t(mesh->heightVertices.size());mesh->heightVertices.push_back({float(x),h,float(z),old,0,2});return index;
    };
    for(int z=0;z<16;z+=in.step){if(cancel&&*cancel)return {};for(int x=0;x<16;x+=in.step){
        // Clockwise viewed from above produces +Y-facing triangles.
        std::vector<std::pair<int,int>> ring;
        int s=in.step;
        for(int t=0;t<s;t+=(x==0?1:s))ring.push_back({x,z+t});
        for(int t=0;t<s;t+=(z+s==16?1:s))ring.push_back({x+t,z+s});
        for(int t=0;t<s;t+=(x+s==16?1:s))ring.push_back({x+s,z+s-t});
        for(int t=0;t<s;t+=(z==0?1:s))ring.push_back({x+s-t,z});
        auto center=vertex(x+s/2,z+s/2);
        std::vector<std::uint32_t> ids;for(auto p:ring)ids.push_back(vertex(p.first,p.second));
        for(std::size_t i=0;i<ids.size();++i)mesh->indices.insert(mesh->indices.end(),{center,ids[i],ids[(i+1)%ids.size()]});
    }}
    // Deep skirts are a safety fallback, not a substitute for shared-edge topology.
    for(int edge=0;edge<4;++edge)if(in.skirts[edge])for(int t=0;t<16;++t){
        int x=edge==0?0:edge==1?16:t,z=edge==2?0:edge==3?16:t;
        int nx=edge<2?x:x+1,nz=edge<2?z+1:z;
        float a=height(x,z),b=height(nx,nz);auto start=std::uint32_t(mesh->heightVertices.size());
        std::uint32_t face=edge==0?0:edge==1?1:edge==2?4:5;
        for(auto v:std::array<HeightVertex,4>{{{float(x),a,float(z),a,8,face},{float(nx),b,float(nz),b,8,face},
            {float(nx),b-8,float(nz),b-8,12,face},{float(x),a-8,float(z),a-8,12,face}}})mesh->heightVertices.push_back(v);
        if(edge==0||edge==3)mesh->indices.insert(mesh->indices.end(),{start,start+2,start+1,start,start+3,start+2});
        else mesh->indices.insert(mesh->indices.end(),{start,start+1,start+2,start,start+2,start+3});
    }
    // Close the wedge between interpolated far edges and each integer voxel column.
    for(int edge=0;edge<4;++edge)if(in.voxelEdge[edge])for(int t=0;t<16;++t){
        int x=edge==0?0:edge==1?16:t,z=edge==2?0:edge==3?16:t;
        int nx=edge<2?x:x+1,nz=edge<2?z+1:z;
        float a=height(x,z),b=height(nx,nz),voxel=in.edges[edge][t];
        if(a==voxel&&b==voxel)continue;
        auto start=std::uint32_t(mesh->heightVertices.size());std::uint32_t face=edge==0?0:edge==1?1:edge==2?4:5;
        for(auto v:std::array<HeightVertex,4>{{{float(x),a,float(z),a,8,face},{float(nx),b,float(nz),b,8,face},
            {float(nx),voxel,float(nz),voxel,8,face},{float(x),voxel,float(z),voxel,8,face}}})mesh->heightVertices.push_back(v);
        auto triangle=[&](std::uint32_t ia,std::uint32_t ib,std::uint32_t ic){
            auto a=mesh->heightVertices[ia],b=mesh->heightVertices[ib],c=mesh->heightVertices[ic];
            double nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);
            double nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            double outward=edge==0?-nx:edge==1?nx:edge==2?-nz:nz;
            if(outward<0)std::swap(ib,ic);
            mesh->indices.insert(mesh->indices.end(),{ia,ib,ic});
        };
        triangle(start,start+1,start+2);triangle(start,start+2,start+3);
    }
    return mesh;
}
float HeightmapStream::morph(const Tile& t)const{
    return std::clamp(float(std::chrono::duration<double>(std::chrono::steady_clock::now()-t.published).count()/0.25),0.0f,1.0f);
}
void HeightmapStream::update(ChunkCoord center,const BorderQuery& query,const std::array<float,3>& direction,const std::function<bool(ChunkCoord)>& voxelVisible,std::uint64_t borderRevision){
    std::vector<Result> results;{std::lock_guard lock(mutex_);results.swap(results_);}
    bool retry=false;
    for(auto& r:results){
        buildMs+=r.ms;
        auto it=tiles_.find(r.job.input.coord);
        if(it==tiles_.end()||it->second!=r.job.tile||*r.job.cancel||r.job.tile->revision!=r.job.revision){++stale;continue;}
        if(r.mesh){r.mesh->instanceId=(1ULL<<63)|r.job.tile->instance;r.mesh->revision=r.job.revision;r.mesh->groupId=r.job.group;
            r.job.tile->pending=r.mesh;r.job.tile->state=GenerationState::Ready;r.job.tile->retries=0;++completed;}
        else if(*r.job.tile->cancel){r.job.tile->state=GenerationState::Cancelled;++cancelled;}
        else {r.job.tile->state=GenerationState::Failed;++r.job.tile->retries;retry|=r.job.tile->retries<=3;}
    }
    if(initialized_&&center==lastCenter_&&borderRevision==lastBorderRevision_&&!retry)return;
    initialized_=true;lastCenter_=center;lastBorderRevision_=borderRevision;
    for(auto it=tiles_.begin();it!=tiles_.end();)if(radius(it->first,center)>24||
        (radius(it->first,center)<=8&&(!it->second->active||(voxelVisible?voxelVisible(it->first):bool(query(it->first.x*16,it->first.z*16)))))){*it->second->cancel=true;it=tiles_.erase(it);}else ++it;
    for(auto&[c,t]:tiles_)if(radius(c,center)<=8){
        // A previous far mesh is a handoff fallback, never a new far publication.
        *t->cancel=true;t->group=0;t->pending.reset();t->uploaded=false;
    }
    std::vector<std::shared_ptr<Tile>> changed;
    for(int z=-24;z<=24;++z)for(int x=-24;x<=24;++x){
        int r=std::max(std::abs(x),std::abs(z));if(r<=8)continue;
        ChunkCoord c{center.x+x,center.z+z};
        // Render only supported gameplay coordinates; no unchecked multiplication.
        if(c.x<-WorldLimit/16||c.x>=WorldLimit/16||c.z<-WorldLimit/16||c.z>=WorldLimit/16)continue;
        auto& tile=tiles_[c];bool created=!tile;
        if(created){tile=std::make_shared<Tile>();tile->instance=nextInstance_++;tile->input.coord=c;}
        HeightTileInput next=tile->input;next.step=HeightmapMesher::selectStep(r,tile->active?tile->active->lod:0);
        next.voxelEdge.fill(false);
        for(auto& edge:next.edges)edge.fill(0);
        std::array<ChunkCoord,4> neighbors{{{c.x-1,c.z},{c.x+1,c.z},{c.x,c.z-1},{c.x,c.z+1}}};
        for(int e=0;e<4;++e)if(radius(neighbors[e],center)<=8)for(int t=0;t<=16;++t){
            auto wx=c.x*16+(e==0?-1:e==1?16:t==16?15:t),wz=c.z*16+(e==2?-1:e==3?16:t==16?15:t);
            if(auto h=query(wx,wz)){next.voxelEdge[e]=true;next.edges[e][t]=*h;}
        }
        for(int e=0;e<4;++e)next.skirts[e]=radius(neighbors[e],center)>24||next.voxelEdge[e];
        if(created)changed.push_back(tile);
        else if(next.step!=tile->input.step||next.voxelEdge!=tile->input.voxelEdge||next.edges!=tile->input.edges||next.skirts!=tile->input.skirts||
            (tile->state==GenerationState::Failed&&tile->retries<=3))changed.push_back(tile);
        else continue;
        tile->input=next;
    }
    if(!changed.empty()){
        // Merge overlapping publication batches. Every pending member shares a group.
        group_=(1ULL<<63)|nextGroup_++;
        for(auto& [c,t]:tiles_)if(t->group&&!t->uploaded&&std::find(changed.begin(),changed.end(),t)==changed.end())changed.push_back(t);
        for(auto& [c,t]:tiles_)if(t->group&&t->uploaded&&std::find(changed.begin(),changed.end(),t)==changed.end())changed.push_back(t);
        std::lock_guard lock(mutex_);
        for(auto& t:changed){*t->cancel=true;t->cancel=std::make_shared<std::atomic<bool>>(false);
            ++t->revision;t->group=group_;t->pending.reset();t->uploaded=false;t->state=GenerationState::Requested;
            t->input.previous=t->active;t->input.previousMorph=morph(*t);
            double dx=double(t->input.coord.x-center.x),dz=double(t->input.coord.z-center.z),length=std::hypot(dx,dz);
            int cameraPriority=length>0?int(25*(direction[0]*dx+direction[2]*dz)/length):0;
            jobs_.push_back({t,t->input,t->revision,t->group,radius(t->input.coord,center)*100-cameraPriority,std::chrono::steady_clock::now(),t->cancel});
        }
        jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),[](const Job& j){return j.revision!=j.tile->revision;}),jobs_.end());
    }
}
bool HeightmapStream::hasJobs()const{std::lock_guard lock(mutex_);return !jobs_.empty();}
bool HeightmapStream::work(const ChunkGenerator& gen){
    Job job;{std::lock_guard lock(mutex_);if(jobs_.empty())return false;
        auto now=std::chrono::steady_clock::now();
        auto best=std::min_element(jobs_.begin(),jobs_.end(),[&](const Job&a,const Job&b){
            auto score=[&](const Job&j){return j.priority-std::chrono::duration<double>(now-j.queued).count()*100;};return score(a)<score(b);});
        job=*best;jobs_.erase(best);
        if(!*job.cancel)job.tile->state=GenerationState::Generating;
    }
    Result result;result.job=job;
    auto start=std::chrono::steady_clock::now();
    try{result.mesh=HeightmapMesher::build(gen,job.input,job.cancel.get());}catch(...){result.failed=true;}
    result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    {std::lock_guard lock(mutex_);results_.push_back(std::move(result));}return true;
}
void HeightmapStream::stop(){for(auto&[c,t]:tiles_)*t->cancel=true;std::lock_guard lock(mutex_);jobs_.clear();}
void HeightmapStream::voxelPublished(ChunkCoord coord){
    auto it=tiles_.find(coord);if(it==tiles_.end())return;
    *it->second->cancel=true;tiles_.erase(it);
}
std::vector<std::shared_ptr<HeightmapStream::Tile>> HeightmapStream::pending()const{
    std::vector<std::shared_ptr<Tile>> out;for(auto&[c,t]:tiles_)if(t->pending&&!t->uploaded)out.push_back(t);return out;
}
std::vector<std::shared_ptr<HeightmapStream::Tile>> HeightmapStream::ready()const{
    std::vector<std::shared_ptr<Tile>> out;
    for(auto&[c,t]:tiles_)if(t->group){if(!t->pending||!t->uploaded)return {};out.push_back(t);}
    return out;
}
bool HeightmapStream::commit(const std::vector<std::shared_ptr<Tile>>& group){
    auto members=ready();if(members.empty()||members.size()!=group.size())return false;
    auto now=std::chrono::steady_clock::now();
    for(auto&t:members){t->active=t->pending;t->pending.reset();t->input.previous.reset();t->group=0;t->published=now;}return true;
}
std::vector<std::shared_ptr<HeightmapStream::Tile>> HeightmapStream::visible(ChunkCoord center)const{
    std::vector<std::shared_ptr<Tile>> out;for(auto&[c,t]:tiles_)if(t->active&&radius(c,center)<=24)out.push_back(t);return out;
}
std::vector<ChunkCoord> HeightmapStream::coords()const{
    std::vector<ChunkCoord> out;for(auto&[c,t]:tiles_)out.push_back(c);return out;
}
bool HeightmapStream::settled()const {
    for(auto&[c,t]:tiles_)if(t->group||!t->active)return false;
    return !hasJobs();
}
}
