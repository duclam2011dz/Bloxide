#pragma once

#include "bloxide/Camera.hpp"
#include "bloxide/World.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace bloxide {

class Renderer {
public:
    struct Vertex {
        float x, y, z;
        float r, g, b;
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    void rebuildMesh(const World& world);
    void render(const Camera& camera, int width, int height);

private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int program_ = 0;
    std::vector<Vertex> vertices_;
};

} // namespace bloxide
