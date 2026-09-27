#pragma once

#include "bloxide/Camera.hpp"
#include "bloxide/World.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bloxide {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    bool upload(const ChunkUpload& upload);
    void render(const Camera& camera, const std::vector<RenderChunk>& chunks, int width, int height);
    [[nodiscard]] std::size_t drawCalls() const noexcept { return drawCalls_; }
    [[nodiscard]] std::size_t uploadedBytes() const noexcept { return uploadedBytes_; }

private:
    struct GpuMesh {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        std::uint32_t indexCount = 0;
        std::uint64_t version = 0;
    };

    void destroy(GpuMesh& mesh);
    unsigned int program_ = 0;
    std::unordered_map<ChunkCoord, GpuMesh, ChunkCoordHash> meshes_;
    std::size_t drawCalls_ = 0;
    std::size_t uploadedBytes_ = 0;
};

} // namespace bloxide
