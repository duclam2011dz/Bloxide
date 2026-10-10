#pragma once
#include <bloxide/Camera.hpp>
#include <bloxide/World.hpp>
#include <glad/glad.h>
#include <unordered_map>
#include <thread>
namespace bloxide {
class Renderer {
public:
    Renderer()=default;
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool initialize(const std::string& vertexShaderPath,const std::string& fragmentShaderPath);
    bool upload(const ChunkUpload& upload);
    bool publish(const UploadGroup& group);
    void synchronize(const std::vector<ChunkCoord>& loaded);
    void shutdown();
    void render(const Camera& camera,const std::vector<RenderChunk>& chunks,int width,int height);
    void renderTarget(const Camera& camera,const BlockTarget& target,int width,int height,bool crosshair=true);
    std::size_t drawCalls()const{return drawCalls_;}
    std::size_t uploadedBytes()const{return uploadedBytes_;}
    std::size_t swaps()const{return swaps_;}
    std::size_t retiredCount()const{return retired_.size();}
    void failNextUploadForTests(ChunkCoord coord){failNextUpload_=true;failCoord_=coord;}
private:
    struct GpuMesh {
        unsigned int vao=0,vbo=0,ebo=0;
        std::uint32_t indexCount=0;
        std::uint64_t instanceId=0,revision=0;
    };
    struct Retired {GpuMesh mesh;GLsync fence=nullptr;};
    void destroy(GpuMesh& mesh);
    void retire(GpuMesh mesh);
    void collectRetired();
    void assertThread()const;
    unsigned int program_=0,overlayProgram_=0,overlayVao_=0,overlayVbo_=0;
    std::unordered_map<ChunkCoord,GpuMesh,ChunkCoordHash> meshes_,pending_;
    std::vector<Retired> retired_;
    std::thread::id owner_;
    std::size_t drawCalls_=0,uploadedBytes_=0,swaps_=0;
    bool failNextUpload_=false;
    ChunkCoord failCoord_{};
};
}
