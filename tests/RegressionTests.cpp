#include <bloxide/Physics.hpp>
#include <bloxide/ChunkGenerator.hpp>
#include <bloxide/ChunkMesher.hpp>
#include <bloxide/World.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

using namespace bloxide;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a-b) < 0.001f; }
std::array<int,3> decode(ChunkMeshVertex v) {
    return {static_cast<int>(v.packed & 31U), static_cast<int>((v.packed >> 5U) & 511U), static_cast<int>((v.packed >> 14U) & 31U)};
}
void geometry() {
    auto data = std::make_shared<ChunkData>();
    data->blocks.fill(BlockType::Air);
    data->set(15,255,15,BlockType::Stone);
    ChunkMesher mesher;
    auto mesh = mesher.build({},data,{},0);
    check(mesh->vertices.size() == 24 && mesh->indices.size() == 36, "isolated block faces");
    const int normals[6][3]{{-1,0,0},{1,0,0},{0,1,0},{0,-1,0},{0,0,-1},{0,0,1}};
    bool maxX=false, maxY=false, maxZ=false;
    for (auto v : mesh->vertices) {
        auto p=decode(v); maxX |= p[0]==16; maxY |= p[1]==256; maxZ |= p[2]==16;
        check(p[0]>=15 && p[2]>=15 && p[1]>=255, "packed boundary wraps");
    }
    check(maxX && maxY && maxZ,"far boundaries preserved");
    for (std::size_t i=0;i<mesh->indices.size();i+=3) {
        auto a=decode(mesh->vertices[mesh->indices[i]]);
        auto b=decode(mesh->vertices[mesh->indices[i+1]]);
        auto c=decode(mesh->vertices[mesh->indices[i+2]]);
        int u[3]{b[0]-a[0],b[1]-a[1],b[2]-a[2]};
        int v[3]{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        int n[3]{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        const auto face=(mesh->vertices[mesh->indices[i]].packed>>22U)&7U;
        check(n[0]*normals[face][0]+n[1]*normals[face][1]+n[2]*normals[face][2]>0,"outward CCW winding");
    }
    ChunkGenerator generator;
    auto flat=generator.generate({});
    auto lod=mesher.build({},flat,{},1);
    check(lod->vertices.size()>4,"heightmap LOD geometry");
    flat->set(8,ChunkGenerator::surfaceHeight(8,8),8,BlockType::Air);
    check(mesher.build({},flat,{},1)->vertices.size()==mesher.build({},flat,{},0)->vertices.size(),"LOD preserves edits");
}
void physics() {
    PlayerPhysics p;
    p.feet={0.5f,10.0f,0.5f};
    const auto floor=[](int,int y,int){ return y<=0; };
    for(int i=0;i<300;++i) p.step(1.0f/120,0,0,false,floor);
    check(near(p.feet[1],1) && p.grounded,"land on actual floor");
    check(near(p.eye()[1],2.62f),"camera eye offset");
    p.step(1.0f/120,0,0,true,floor);
    check(p.feet[1]>1 && !p.grounded,"jump only from ground");
    for(int i=0;i<180;++i) p.step(1.0f/120,0,0,false,floor);
    check(near(p.feet[1],1) && p.grounded,"jump lands");
    const auto wall=[](int x,int y,int){ return y<=0 || (x==2 && y<4); };
    for(int i=0;i<60;++i) p.step(1.0f/60,1,1,false,wall);
    check(p.feet[0]<=1.701f && p.feet[2]>2,"wall blocks X and permits sliding");
    p.feet={-0.5f,1,-0.5f}; p.velocity={}; p.grounded=true;
    const auto ceiling=[](int,int y,int){return y<=0 || y==3;};
    p.step(0.1f,0,0,true,ceiling);
    check(p.feet[1]+PlayerPhysics::Height<=3.001f && p.velocity[1]<=0,"head collision");
    p.feet={0.5f,2,0.5f}; p.velocity={}; p.grounded=true;
    p.step(0.1f,0,0,false,floor);
    check(p.feet[1]<2,"removed support causes fall");
    p.feet={0.5f,10,0.5f}; p.velocity={0,-40,0};
    p.step(0.25f,0,0,false,floor);
    check(near(p.feet[1],1),"long frame cannot tunnel floor");
    check(p.overlapsBlock(0,1,0) && !p.overlapsBlock(0,0,0),"placement overlap");
    for (float dt : {1.0f/30,1.0f/60,1.0f/144}) {
        p.feet={-16.1f,5,-0.1f}; p.velocity={}; p.grounded=false;
        for (int i=0;i<300;++i) p.step(dt,0,0,false,floor);
        check(near(p.feet[1],1) && p.grounded,"stable grounding at different frame rates");
    }
}
void streaming() {
    World world;
    const auto wait=[&](auto predicate) {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<deadline) {
            world.update({8,65.62f,8},{0,0,-1});
            for(auto u:world.takePendingUploads()) world.markUploaded(u);
            for(auto g:world.readyGroups()) world.commitGroup(g);
            if(predicate()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("streaming timeout");
    };
    wait([&]{return world.columnLoaded(8,8) && !world.renderChunks({8,65.62f,8},{0,0,-1}).empty();});
    check(!world.set(8,0,8,BlockType::Air),"Bedrock protected");
    const int h=ChunkGenerator::surfaceHeight(8,8);
    check(world.set(8,h,8,BlockType::Air),"dig support");
    check(world.get(8,h,8)==BlockType::Air,"collision sees edit");
    PlayerPhysics p; p.feet={8.5f,static_cast<float>(h+1),8.5f};
    for(int i=0;i<120;++i) p.step(1.0f/120,0,0,false,[&](int x,int y,int z){return isSolid(world.get(x,y,z));});
    check(near(p.feet[1],static_cast<float>(h)) && p.grounded,"player falls to next real block");
    wait([&]{return !world.renderChunks({8,65.62f,8},{0,0,-1}).empty();});
    // Cross the unload radius, then reload and verify the journal preserves the hole.
    world.update({400,65.62f,400},{0,0,-1});
    check(!world.columnLoaded(8,8),"spawn chunk unloaded");
    wait([&]{return world.columnLoaded(8,8);});
    check(world.get(8,h,8)==BlockType::Air && world.get(8,h-1,8)==BlockType::Dirt,
          "journal restores correct block after reload");
}
int main() {
    try { geometry(); physics(); streaming(); std::cout << "geometry, physics, streaming regression tests passed\n"; }
    catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
