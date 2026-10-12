#pragma once
#include <bloxide/Camera.hpp>
#include <bloxide/World.hpp>
#include <glad/glad.h>
#include <unordered_map>
#include <thread>
#include <string>
namespace bloxide {
class Renderer {
public:
    Renderer()=default;
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool initialize(const std::string& vertexShaderPath,const std::string& fragmentShaderPath,const std::string& assetsDirectory);
    bool validateTexturesForTests() const;
    bool validateHeightmapForTests() const;
    bool validatePublishedForTests(const std::vector<RenderChunk>&)const;
    double textureLoadMs()const{return textureLoadMs_;}
    double textureUploadMs()const{return textureUploadMs_;}
    bool upload(const ChunkUpload& upload);
    bool publish(const UploadGroup& group);
    void synchronize(const std::vector<ChunkCoord>& loaded);
    void shutdown();
    void render(const Camera& camera,const std::vector<RenderChunk>& chunks,int width,int height);
    void renderTarget(const Camera& camera,const BlockTarget& target,int width,int height,bool crosshair=true);
    std::size_t drawCalls()const{return drawCalls_;}
    double gpuTerrainMs()const{return gpuTerrainMs_;}
    std::size_t uploadedBytes()const{return uploadedBytes_;}
    std::size_t swaps()const{return swaps_;}
    std::size_t retiredCount()const{return retired_.size();}
    void failNextUploadForTests(ChunkCoord coord){failNextUpload_=true;failCoord_=coord;}
private:
    struct GpuMesh {
        unsigned int vao=0,vbo=0,ebo=0;
        std::uint32_t indexCount=0;
        std::uint64_t instanceId=0,revision=0;
        bool heightmap=false;
    };
    struct Retired {GpuMesh mesh;GLsync fence=nullptr;};
    void destroy(GpuMesh& mesh);
    void retire(GpuMesh mesh);
    void collectRetired();
    void assertThread()const;
    unsigned int program_=0,overlayProgram_=0,overlayVao_=0,overlayVbo_=0;
    unsigned int textures_=0;
    unsigned int heightmapProgram_=0;
    std::array<GLuint,8> queries_{};
    std::array<bool,8> queryPending_{};
    std::size_t queryCursor_=0;
    double gpuTerrainMs_=0;
    std::uint32_t textureSeed_=125;
    double textureLoadMs_=0,textureUploadMs_=0;
    std::string assetsDirectory_;
    std::unordered_map<ChunkCoord,GpuMesh,ChunkCoordHash> meshes_,pending_;
    std::vector<Retired> retired_;
    std::thread::id owner_;
    std::size_t drawCalls_=0,uploadedBytes_=0,swaps_=0;
    bool failNextUpload_=false;
    ChunkCoord failCoord_{};
};
}
