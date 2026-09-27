#include "bloxide/Renderer.hpp"

#include <glad/glad.h>

#include <SDL2/SDL.h>

#include <array>
#include <fstream>
#include <sstream>

namespace bloxide {
namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int compileShader(unsigned int type, const std::string& source) {
    const unsigned int shader = glCreateShader(type);
    const char* sourceText = source.c_str();
    glShaderSource(shader, 1, &sourceText, nullptr);
    glCompileShader(shader);
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void addFace(std::vector<Renderer::Vertex>& vertices, int x, int y, int z, BlockType block, int face) {
    const Color c = blockColor(block);
    const float shade = face == 2 ? 1.0f : (face == 3 ? 0.78f : 0.88f);
    const float x0 = static_cast<float>(x), x1 = x0 + 1.0f;
    const float y0 = static_cast<float>(y), y1 = y0 + 1.0f;
    const float z0 = static_cast<float>(z), z1 = z0 + 1.0f;
    const auto v = [&](float px, float py, float pz) { vertices.push_back({px, py, pz, c.r * shade, c.g * shade, c.b * shade}); };
    switch (face) {
    case 0: v(x0,y0,z0); v(x0,y1,z0); v(x0,y1,z1); v(x0,y0,z0); v(x0,y1,z1); v(x0,y0,z1); break;
    case 1: v(x1,y0,z1); v(x1,y1,z1); v(x1,y1,z0); v(x1,y0,z1); v(x1,y1,z0); v(x1,y0,z0); break;
    case 2: v(x0,y1,z0); v(x1,y1,z0); v(x1,y1,z1); v(x0,y1,z0); v(x1,y1,z1); v(x0,y1,z1); break;
    case 3: v(x0,y0,z1); v(x1,y0,z1); v(x1,y0,z0); v(x0,y0,z1); v(x1,y0,z0); v(x0,y0,z0); break;
    case 4: v(x1,y0,z0); v(x1,y1,z0); v(x0,y1,z0); v(x1,y0,z0); v(x0,y1,z0); v(x0,y0,z0); break;
    case 5: v(x0,y0,z1); v(x0,y1,z1); v(x1,y1,z1); v(x0,y0,z1); v(x1,y1,z1); v(x1,y0,z1); break;
    default: break;
    }
}

} // namespace

Renderer::~Renderer() {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (program_ != 0) glDeleteProgram(program_);
}

bool Renderer::initialize(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
    const unsigned int vertex = compileShader(GL_VERTEX_SHADER, readFile(vertexShaderPath));
    const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, readFile(fragmentShaderPath));
    if (vertex == 0 || fragment == 0) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    int linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return true;
}

void Renderer::rebuildMesh(const World& world) {
    vertices_.clear();
    constexpr int dx[] = {-1, 1, 0, 0, 0, 0};
    constexpr int dy[] = {0, 0, 1, -1, 0, 0};
    constexpr int dz[] = {0, 0, 0, 0, -1, 1};
    for (int y = 0; y < World::Height; ++y) {
        for (int z = 0; z < World::Depth; ++z) {
            for (int x = 0; x < World::Width; ++x) {
                const BlockType block = world.get(x, y, z);
                if (!isSolid(block)) continue;
                for (int face = 0; face < 6; ++face) {
                    if (!isSolid(world.get(x + dx[face], y + dy[face], z + dz[face]))) addFace(vertices_, x, y, z, block, face);
                }
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices_.size() * sizeof(Vertex)), vertices_.data(), GL_STATIC_DRAW);
}

void Renderer::render(const Camera& camera, int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(0.48f, 0.73f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    const auto view = camera.viewMatrix();
    const auto projection = camera.projectionMatrix(static_cast<float>(width) / static_cast<float>(height));
    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, view.data());
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"), 1, GL_FALSE, projection.data());
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices_.size()));
}

} // namespace bloxide
