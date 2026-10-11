#include <bloxide/World.hpp>
#include <bloxide/Physics.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace bloxide;
void require(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
void terrain() {
    WorldSettings settings;settings.caveThreshold=0.99999;
    ChunkGenerator gen(settings);
    for(int x=-140;x<140;x+=7)for(int z=-140;z<140;z+=9){
        int h=gen.surfaceHeight(x,z);
        require(h>=24&&h<=192,"height bounds");
        require(h==gen.heightAt(x,z),"deterministic height");
    }
    for(ChunkCoord c:std::vector<ChunkCoord>{{-1,-1},{0,0},{1,1}}){
        auto data=gen.generate(c);
        for(int x=0;x<16;++x)for(int z=0;z<16;++z){
            int h=gen.surfaceHeight(c.x*16+x,c.z*16+z);
            require(data->get(x,h,z)==BlockType::Grass&&data->get(x,h+1,z)==BlockType::Air,"world-height seam");
            require(data->get(x,h-3,z)==BlockType::Dirt&&data->get(x,h-4,z)==BlockType::Stone,"layers");
            require(data->get(x,0,z)==BlockType::Bedrock,"Bedrock");
        }
    }
    auto center=gen.generate({0,0});
    ChunkMesher mesher;auto coarse=mesher.build({},center,{},1);
    int height[16][16]{};
    for(std::size_t i=0;i<coarse->vertices.size();i+=4){
        auto p=coarse->vertices[i].packed;
        if(((p>>24U)&7U)!=2)continue;
        int x0=16,z0=16,x1=0,z1=0,y=0;
        for(int j=0;j<4;++j){auto v=coarse->vertices[i+j].packed;int x=v&31U,z=(v>>14U)&31U;x0=std::min(x0,x);x1=std::max(x1,x);z0=std::min(z0,z);z1=std::max(z1,z);y=(v>>5U)&511U;}
        for(int x=x0;x<x1;++x)for(int z=z0;z<z1;++z)height[x][z]=std::max(height[x][z],y-1);
    }
    for(int x=0;x<16;++x)for(int z=0;z<16;++z){
        int actual=gen.surfaceHeight(x,z);
        require(height[x][z]>=actual,"LOD conservative surface");
        if(x==0||z==0||x==15||z==15)require(height[x][z]==actual,"LOD exact perimeter");
    }
    center->set(8,gen.surfaceHeight(8,8),8,BlockType::Air);
    auto full=mesher.build({},center,{},0),edited=mesher.build({},center,{},1);
    require(full->indices==edited->indices&&full->vertices.size()==edited->vertices.size(),"edited LOD uses full geometry");
}
void targeting() {
    auto at=[](int x,int y,int z){return TargetVoxel{x==6&&y==0&&z==0?BlockType::Stone:BlockType::Air,false,7,11};};
    auto hit=traceVoxels({0,0.5f,0.5f},{1,0,0},6,at);
    require(hit.hit&&hit.distance==6&&hit.normal==Int3{-1,0,0},"reach equality and normal");
    require(!traceVoxels({-0.001f,0.5f,0.5f},{1,0,0},6,at).hit,"beyond reach");
    auto negative=traceVoxels({-0.5f,0.5f,0.5f},{-1,0,0},6,[](int x,int,int){
        return TargetVoxel{x==-2?BlockType::Bedrock:BlockType::Air,false,1,1};});
    require(negative.hit&&negative.block.x==-2&&negative.normal.x==1&&std::abs(negative.distance-0.5f)<0.0001f,"negative coordinate DDA");
    auto wall=[](int x,int,int){return TargetVoxel{x==2||x==4?BlockType::Stone:BlockType::Air,false,1,1};};
    require(traceVoxels({0.5f,0.5f,0.5f},{1,0,0},6,wall).block.x==2,"first solid occludes");
    auto blocked=[](int x,int,int){return TargetVoxel{x==4?BlockType::Stone:BlockType::Air,x==2,1,1};};
    require(!traceVoxels({0.5f,0.5f,0.5f},{1,0,0},6,blocked).hit,"blocked chunks stop ray");
    require(!traceVoxels({0,0,0},{0,0,0},6,wall).hit,"zero ray");
    auto corner=traceVoxels({0.5f,0.5f,0.5f},{1,1,1},6,[](int x,int y,int z){return TargetVoxel{x==1&&y==1&&z==1?BlockType::Stone:BlockType::Air,false,1,1};});
    require(corner.hit&&corner.previous==Int3{1,1,0}&&corner.normal==Int3{0,0,-1},"deterministic corner crossing");
}
void lifecycle() {
    WorldSettings settings;settings.caveThreshold=0.99999;
    World world(settings);std::array<float,3> position{8,128,8};
    auto tick=[&](bool uploads=true){
        world.update(position,{0,0,-1});
        if(uploads){
            for(auto&u:world.takePendingUploads())require(world.markUploaded(u),"upload token");
            for(auto&g:world.readyGroups())require(world.commitGroup(g),"publish token");
        }
    };
    auto wait=[&](auto pred,bool uploads=true){
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(25);
        while(std::chrono::steady_clock::now()<end){tick(uploads);if(pred())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        throw std::runtime_error("lifecycle timeout");
    };
    auto meshFor=[&](ChunkCoord c){
        for(auto r:world.renderChunks(position,{0,0,0}))if(r.coord==c)return r.mesh;
        return std::shared_ptr<const ChunkMesh>{};
    };
    wait([&]{return world.targetable(15,8)&&world.targetable(16,8)&&world.stats().generationJobs>=441&&world.stats().ready>=289;});
    auto old0=meshFor({0,0}),old1=meshFor({1,0});
    require(old0&&old1,"initial meshes");
    int h=ChunkGenerator::surfaceHeight(15,8);
    auto presented=world.target({15.5f,float(h+3),8.5f},{0,-1,0});
    require(presented.hit&&presented.block==Int3{15,h,8},"shared target");
    require(!world.validateTarget(presented,{15.5f,float(h+3),8.5f},{1,0,0}),"camera target changed");
    world.setMeshDelayForTests(60);
    world.failNextMeshForTests();
    require(world.set(15,h,8,BlockType::Air),"boundary edit");
    require(!world.targetable(15,8)&&!world.targetable(16,8),"group locks");
    require(!world.validateTarget(presented,{15.5f,float(h+3),8.5f},{0,-1,0}),"stale target rejected");
    require(meshFor({0,0})==old0&&meshFor({1,0})==old1,"active retained before remesh");
    wait([&]{
        bool a=false,b=false;
        for(auto&u:world.takePendingUploads()){a|=u.coord==ChunkCoord{0,0};b|=u.coord==ChunkCoord{1,0};}
        return a&&b;
    },false);
    auto pending=world.takePendingUploads();
    require(world.stats().failedJobs>=1,"meshing failure retries");
    require(meshFor({0,0})==old0&&meshFor({1,0})==old1,"failed meshing preserves active group");
    auto changed=std::find_if(pending.begin(),pending.end(),[](auto&u){return u.coord==ChunkCoord{0,0};});
    require(changed!=pending.end(),"new CPU mesh");
    auto stale=*changed;
    auto groupId=changed->mesh->groupId;
    std::vector<ChunkUpload> group;
    for(auto&u:pending)if(u.mesh->groupId==groupId)group.push_back(u);
    require(group.size()==2,"only dependent border neighbor");
    require(world.markUploaded(group[0]),"first member staged");
    for(auto&g:world.readyGroups())require(g.id!=groupId,"partial upload cannot publish");
    require(meshFor({0,0})==old0,"active stays after partial upload");
    require(world.set(16,ChunkGenerator::surfaceHeight(16,8),8,BlockType::Air),"overlap edit");
    require(!world.markUploaded(stale),"stale revision rejected");
    wait([&]{return world.targetable(15,8)&&world.targetable(16,8);});
    require(meshFor({0,0})!=old0&&meshFor({1,0})!=old1,"atomic group swap");
    require(world.get(15,h,8)==BlockType::Air,"new collision data");
    world.setMeshDelayForTests(0);
    wait([&]{return meshFor({5,0})&&meshFor({5,0})->lod==0;});
    auto lod=meshFor({5,0});position={88,80,8};tick(false);
    require(meshFor({5,0})==lod,"full cave mesh remains active across distance changes");
    wait([&]{return meshFor({5,0})&&meshFor({5,0})->lod==0&&world.targetable(88,8);});
    auto initialInstance=meshFor({0,0})->instanceId;
    position={600,80,600};tick(false);
    require(!world.columnLoaded(15,8)&&!world.markUploaded(stale),"unloaded result rejected");
    position={8,80,8};
    wait([&]{return world.targetable(15,8);});
    require(meshFor({0,0})->instanceId!=initialInstance,"reload instance differs");
    require(world.get(15,h,8)==BlockType::Air,"journal retains edit");
    std::cout<<"swaps="<<world.stats().swaps<<" stale_results="<<world.stats().staleResults<<" edit_publish_ms="<<world.stats().lastPublishMs<<std::endl;
}
int main(){try{terrain();targeting();lifecycle();std::cout<<"v1.2 terrain, targeting and lifecycle passed"<<std::endl;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
