#include "bloxide/Renderer.hpp"

#include <glad/glad.h>

#include <fstream>
#include <sstream>

namespace bloxide {
namespace {
std::string readFile(const std::string& path) { std::ifstream file(path); std::stringstream buffer; buffer << file.rdbuf(); return buffer.str(); }

unsigned int compileShader(unsigned int type, const std::string& source) {
    const unsigned int shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { glDeleteShader(shader); return 0; }
    return shader;
}
}

Renderer::~Renderer() {
    for (auto& [coord, mesh] : meshes_) { (void)coord; destroy(mesh); }
    if (program_ != 0) glDeleteProgram(program_);
}

void Renderer::destroy(GpuMesh& mesh) {
    if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    mesh = {};
}

bool Renderer::initialize(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
    const unsigned int vertex = compileShader(GL_VERTEX_SHADER, readFile(vertexShaderPath));
    const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, readFile(fragmentShaderPath));
    if (!vertex || !fragment) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vertex); glAttachShader(program_, fragment); glLinkProgram(program_);
    glDeleteShader(vertex); glDeleteShader(fragment);
    int linked = 0; glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}

bool Renderer::upload(const ChunkUpload& upload) {
    if (!upload.mesh) return false;
    auto& mesh = meshes_[upload.coord];
    if (mesh.version >= upload.mesh->sourceVersion && mesh.vao != 0) return false;
    destroy(mesh);
    glGenVertexArrays(1, &mesh.vao); glGenBuffers(1, &mesh.vbo); glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(upload.mesh->vertices.size() * sizeof(ChunkMeshVertex)), upload.mesh->vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(upload.mesh->indices.size() * sizeof(std::uint32_t)), upload.mesh->indices.data(), GL_STATIC_DRAW);
    glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(ChunkMeshVertex), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    mesh.indexCount = static_cast<std::uint32_t>(upload.mesh->indices.size());
    mesh.version = upload.mesh->sourceVersion;
    uploadedBytes_ += upload.mesh->byteSize();
    return true;
}

void Renderer::render(const Camera& camera, const std::vector<RenderChunk>& chunks, int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(0.48f, 0.73f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    const auto view = camera.viewMatrix();
    const auto projection = camera.projectionMatrix(static_cast<float>(width) / static_cast<float>(height));
    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, view.data());
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"), 1, GL_FALSE, projection.data());
    const auto& cameraPosition = camera.position();
    drawCalls_ = 0;
    for (const auto& chunk : chunks) {
        const auto it = meshes_.find(chunk.coord);
        if (it == meshes_.end() || it->second.indexCount == 0) continue;
        const float cx = static_cast<float>(chunk.coord.x * ChunkData::Width);
        const float cz = static_cast<float>(chunk.coord.z * ChunkData::Depth);
        const float dx = cx + 8.0f - cameraPosition[0], dz = cz + 8.0f - cameraPosition[2];
        if (dx * dx + dz * dz > 180.0f * 180.0f) continue;
        glUniform3f(glGetUniformLocation(program_, "uChunkOrigin"), cx, 0.0f, cz);
        glBindVertexArray(it->second.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(it->second.indexCount), GL_UNSIGNED_INT, nullptr);
        ++drawCalls_;
    }
}

} // namespace bloxide
