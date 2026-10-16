#include <bloxide/Renderer.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <bloxide/Textures.hpp>
#include <chrono>
#include <filesystem>
#include <bloxide/Trace.hpp>
namespace bloxide {
namespace {
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
GLuint shader(GLenum type, const std::string& source) {
    GLuint id = glCreateShader(type);
    const char* s = source.c_str();
    glShaderSource(id, 1, &s, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(id);
        return 0;
    }
    return id;
}
GLuint program(const std::string& vs, const std::string& fs) {
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v)
            glDeleteShader(v);
        if (f)
            glDeleteShader(f);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
using Clip = std::array<float, 4>;
Clip transform(const std::array<float, 16>& m, const Clip& v) {
    Clip out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r] += m[c * 4 + r] * v[c];
    return out;
}
void ribbon(std::vector<Clip>& verts, Clip a, Clip b, int width, int height, float pixels) {
    // Clip segment against the near plane before perspective division.
    float da = a[2] + a[3], db = b[2] + b[3];
    if (da < 0 && db < 0)
        return;
    if (da < 0 || db < 0) {
        float t = da / (da - db);
        Clip cut{};
        for (int i = 0; i < 4; ++i)
            cut[i] = a[i] + (b[i] - a[i]) * t;
        if (da < 0)
            a = cut;
        else
            b = cut;
    }
    if (a[3] <= 0.000001f || b[3] <= 0.000001f)
        return;
    float dx = (b[0] / b[3] - a[0] / a[3]) * width, dy = (b[1] / b[3] - a[1] / a[3]) * height;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.00001f)
        return;
    float ox = -dy / len * pixels / width, oy = dx / len * pixels / height;
    Clip al = a, ar = a, bl = b, br = b;
    al[0] += ox * a[3];
    al[1] += oy * a[3];
    ar[0] -= ox * a[3];
    ar[1] -= oy * a[3];
    bl[0] += ox * b[3];
    bl[1] += oy * b[3];
    br[0] -= ox * b[3];
    br[1] -= oy * b[3];
    for (Clip* p : {&al, &ar, &bl, &br})
        (*p)[2] -= 0.00002f * (*p)[3];
    verts.insert(verts.end(), {al, ar, bl, ar, br, bl});
}
} // namespace
Renderer::~Renderer() {
    shutdown();
}
void Renderer::assertThread() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("OpenGL must run on render thread");
}
void Renderer::destroy(GpuMesh& m) {
    for (auto* part : {&m.cutout, &m.plants, &m.translucent}) {
        if (part->ebo)
            glDeleteBuffers(1, &part->ebo);
        if (part->vbo)
            glDeleteBuffers(1, &part->vbo);
        if (part->vao)
            glDeleteVertexArrays(1, &part->vao);
    }
    if (m.ebo)
        glDeleteBuffers(1, &m.ebo);
    if (m.coarseEbo)
        glDeleteBuffers(1, &m.coarseEbo);
    if (m.vbo)
        glDeleteBuffers(1, &m.vbo);
    if (m.vao)
        glDeleteVertexArrays(1, &m.vao);
    m = {};
}
void Renderer::retire(GpuMesh m) {
    if (m.vao)
        retired_.push_back({m, glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)});
}
void Renderer::collectRetired() {
    for (auto it = retired_.begin(); it != retired_.end();) {
        GLenum status = it->fence ? glClientWaitSync(it->fence, 0, 0) : GL_ALREADY_SIGNALED;
        if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
            if (it->fence)
                glDeleteSync(it->fence);
            destroy(it->mesh);
            it = retired_.erase(it);
        } else
            ++it;
    }
}
void Renderer::shutdown() {
    if (!program_ && !overlayProgram_ && !textures_ && meshes_.empty() && pending_.empty())
        return;
    assertThread();
    for (auto& [c, m] : meshes_)
        destroy(m);
    meshes_.clear();
    for (auto& [c, m] : pending_)
        destroy(m);
    pending_.clear();
    for (auto& r : retired_) {
        if (r.fence)
            glDeleteSync(r.fence);
        destroy(r.mesh);
    }
    retired_.clear();
    if (overlayVbo_)
        glDeleteBuffers(1, &overlayVbo_);
    if (overlayVao_)
        glDeleteVertexArrays(1, &overlayVao_);
    if (program_)
        glDeleteProgram(program_);
    if (heightmapProgram_)
        glDeleteProgram(heightmapProgram_);
    if (surfaceProgram_)
        glDeleteProgram(surfaceProgram_);
    surfaceProgram_ = 0;
    if (queries_[0])
        glDeleteQueries(32, queries_.data());
    if (frameQueries_[0])
        glDeleteQueries(16, frameQueries_.data());
    frameQueries_.fill(0);
    framePending_.fill(false);
    frameSlot_ = -1;
    queries_.fill(0);
    queryPending_.fill(false);
    heightmapProgram_ = 0;
    if (overlayProgram_)
        glDeleteProgram(overlayProgram_);
    if (textures_)
        glDeleteTextures(1, &textures_);
    textures_ = 0;
    overlayVbo_ = overlayVao_ = program_ = overlayProgram_ = 0;
}
bool Renderer::initialize(const std::string& vp, const std::string& fp,
                          const std::string& assetDir) {
    owner_ = std::this_thread::get_id();
    TextureAssets assets;
    try {
        assets = AssetLoader::load(assetDir);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
    assetsDirectory_ = assetDir;
    textureSeed_ = assets.seed;
    textureLoadMs_ = assets.loadMs;
    GLint maxLayers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    if (maxLayers < int(TextureLayers) || assets.layerCount != TextureLayers) {
        std::cerr << "Renderer requires the complete " << TextureLayers << "-layer catalog"
                  << std::endl;
        return false;
    }
    auto start = std::chrono::steady_clock::now();
    glGenTextures(1, &textures_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 32, 32, TextureLayers, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    for (unsigned layer = 0; layer < TextureLayers; ++layer)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, 32, 32, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        assets.layers[layer].rgba.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    for (unsigned layer = 0; layer < TextureLayers; ++layer)
        if (textureRenderClass(layer / 4) == RenderClass::Cutout) {
            auto levels = cutoutMipChain(assets.layers[layer]);
            for (unsigned level = 1; level < levels.size(); ++level) {
                auto& mip = levels[level];
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, int(level), 0, 0, int(layer), int(mip.width),
                                int(mip.height), 1, GL_RGBA, GL_UNSIGNED_BYTE, mip.rgba.data());
            }
        }
    textureUploadMs_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (glGetError() != GL_NO_ERROR) {
        std::cerr << "Texture Array upload failed" << std::endl;
        return false;
    }
    program_ = program(readFile(vp), readFile(fp));
    if (!program_)
        return false;
    heightmapProgram_ =
        program(readFile((std::filesystem::path(vp).parent_path() / "heightmap.vert").string()),
                readFile(fp));
    if (!heightmapProgram_)
        return false;
    surfaceProgram_ =
        program(readFile((std::filesystem::path(vp).parent_path() / "surface.vert").string()),
                readFile(fp));
    if (!surfaceProgram_)
        return false;
    glGenQueries(32, queries_.data());
    glGenQueries(16, frameQueries_.data());
    overlayProgram_ = program(
        "#version 330 core\nlayout(location=0) in vec4 clip;void main(){gl_Position=clip;}\n",
        "#version 330 core\nuniform vec3 color;out vec4 frag;void main(){frag=vec4(color,1);}\n");
    if (!overlayProgram_)
        return false;
    glGenVertexArrays(1, &overlayVao_);
    glGenBuffers(1, &overlayVbo_);
    glBindVertexArray(overlayVao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Clip), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    return true;
}
bool Renderer::validateTexturesForTests() const {
    assertThread();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_);
    for (int level = 0; level < 6; ++level) {
        GLint w = 0, h = 0, d = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, level, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, level, GL_TEXTURE_HEIGHT, &h);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, level, GL_TEXTURE_DEPTH, &d);
        if (w != (32 >> level) || h != w || d != int(TextureLayers))
            return false;
    }
    auto assets = AssetLoader::load(assetsDirectory_);
    std::vector<unsigned char> pixels(TextureLayers * 4096);
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (unsigned layer = 0; layer < TextureLayers; ++layer)
        if (!std::equal(assets.layers[layer].rgba.begin(), assets.layers[layer].rgba.end(),
                        pixels.begin() + layer * 4096))
            return false;
    // Rasterize large quads at one texel per pixel. Compare shader repetition,
    // face orientation and signed-coordinate variant hashing to source PNGs.
    GpuMesh fixture;
    glGenVertexArrays(1, &fixture.vao);
    glGenBuffers(1, &fixture.vbo);
    glGenBuffers(1, &fixture.ebo);
    glBindVertexArray(fixture.vao);
    glBindBuffer(GL_ARRAY_BUFFER, fixture.vbo);
    glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(ChunkMeshVertex), nullptr);
    glEnableVertexAttribArray(0);
    std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fixture.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glViewport(0, 0, 256, 128);
    glDisable(GL_CULL_FACE);
    glUseProgram(program_);
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"), 1, GL_FALSE, identity.data());
    glUniform1i(glGetUniformLocation(program_, "uBlockTextures"), 0);
    glUniform1ui(glGetUniformLocation(program_, "uTextureSeed"), textureSeed_);
    bool correct = true;
    bool reported = false;
    std::vector<unsigned char> rendered(256 * 128 * 4);
    for (std::int64_t offset :
         std::array<std::int64_t, 4>{0, -16, WorldLimit - 32, -WorldLimit + 32})
        for (int face = 0; face < 6; ++face)
            for (unsigned base : {0U, 4U, 8U, 12U, 16U})
                for (unsigned ao : {0U, 3U}) {
                    std::array<std::array<int, 3>, 4> positions{};
                    std::array<float, 16> view{};
                    if (face < 2) {
                        positions = {{{0, 0, 0}, {0, 0, 8}, {0, 4, 8}, {0, 4, 0}}};
                        view = {0, 0, 0, 0, 0, 0.5f, 0, 0, 0.25f, 0, 0, 0, -1, -1, 0, 1};
                    } else if (face < 4) {
                        positions = {{{0, 0, 0}, {8, 0, 0}, {8, 0, 4}, {0, 0, 4}}};
                        view = {0.25f, 0, 0, 0, 0, 0, 0, 0, 0, -0.5f, 0, 0, -1, 1, 0, 1};
                    } else {
                        positions = {{{0, 0, 0}, {8, 0, 0}, {8, 4, 0}, {0, 4, 0}}};
                        view = {0.25f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0, 0, -1, -1, 0, 1};
                    }
                    std::array<ChunkMeshVertex, 4> vertices{};
                    for (int i = 0; i < 4; ++i)
                        vertices[i].packed = positions[i][0] | (positions[i][1] << 5) |
                                             (positions[i][2] << 14) | (base << 19) |
                                             (unsigned(face) << 26) | (ao << 29);
                    glBindBuffer(GL_ARRAY_BUFFER, fixture.vbo);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(),
                                 GL_STATIC_DRAW);
                    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE,
                                       view.data());
                    glUniform3f(glGetUniformLocation(program_, "uChunkOrigin"), 0, 0, 0);
                    auto bits = static_cast<std::uint64_t>(offset);
                    glUniform2ui(glGetUniformLocation(program_, "uChunkBlockX"),
                                 static_cast<GLuint>(bits), static_cast<GLuint>(bits >> 32));
                    glUniform2ui(glGetUniformLocation(program_, "uChunkBlockZ"),
                                 static_cast<GLuint>(bits), static_cast<GLuint>(bits >> 32));
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                    glReadPixels(0, 0, 256, 128, GL_RGBA, GL_UNSIGNED_BYTE, rendered.data());
                    for (int py = 7; py < 128; py += 13)
                        for (int px = 7; px < 256; px += 13) {
                            std::int64_t x = 0, z = 0;
                            int y = 0, tx = px % 32, ty = 31 - py % 32;
                            if (face < 2) {
                                x = offset - (face == 1);
                                y = py / 32;
                                z = offset + px / 32;
                                if (face == 1)
                                    tx = 31 - tx;
                            } else if (face < 4) {
                                x = offset + px / 32;
                                y = face == 2 ? -1 : 0;
                                z = offset + 3 - py / 32;
                                if (face == 3)
                                    ty = py % 32;
                            } else {
                                x = offset + px / 32;
                                y = py / 32;
                                z = offset - (face == 5);
                                if (face == 4)
                                    tx = 31 - tx;
                            }
                            auto layer = base + textureVariant(x, y, z, base, textureSeed_);
                            auto source = (ty * 32 + tx) * 4, target = (py * 256 + px) * 4;
                            for (int c = 0; c < 3; ++c) {
                                int expected = int(std::round(
                                    assets.layers[layer].rgba[source + c] * (1.0f - ao * 0.18f)));
                                if (std::abs(int(rendered[target + c]) - expected) > 2) {
                                    correct = false;
                                    if (!reported) {
                                        std::cerr << "sampling face=" << face << " base=" << base
                                                  << " ao=" << ao << " offset=" << offset
                                                  << " pixel=" << px << ',' << py << " texel=" << tx
                                                  << ',' << ty << " layer=" << layer
                                                  << " actual=" << int(rendered[target + c])
                                                  << " expected=" << expected << std::endl;
                                        reported = true;
                                    }
                                }
                            }
                        }
                }
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    if (fixture.ebo)
        glDeleteBuffers(1, &fixture.ebo);
    if (fixture.vbo)
        glDeleteBuffers(1, &fixture.vbo);
    if (fixture.vao)
        glDeleteVertexArrays(1, &fixture.vao);
    if (!correct) {
        std::cerr << "Texture UV/layer/AO sampling regression failed" << std::endl;
        return false;
    }
    return glGetError() == GL_NO_ERROR;
}
bool Renderer::upload(const ChunkUpload& u) {
    assertThread();
    if (!u.mesh)
        return false;
    if (uploadSpentMs_ >= 2)
        return false;
    auto uploadStarted = std::chrono::steady_clock::now();
    TraceScope trace(TracePhase::Upload, u.coord);
    if (failNextUpload_ && u.coord == failCoord_) {
        failNextUpload_ = false;
        return false;
    }
    auto existing = pending_.find(u.coord);
    if (existing != pending_.end() && existing->second.instanceId == u.mesh->instanceId &&
        existing->second.revision == u.mesh->revision)
        return true;
    auto bytes = allocatedMeshBytes() - (existing != pending_.end() ? existing->second.bytes : 0) +
                 u.mesh->byteSize();
    if (bytes > meshBudget_) {
        budgetPressure_ = true;
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    GpuMesh m;
    auto part = [&](GpuPart& target, const void* vertices, std::size_t bytes,
                    const std::vector<std::uint32_t>& indices, bool surface) {
        if (indices.empty())
            return;
        glGenVertexArrays(1, &target.vao);
        glGenBuffers(1, &target.vbo);
        glGenBuffers(1, &target.ebo);
        glBindVertexArray(target.vao);
        glBindBuffer(GL_ARRAY_BUFFER, target.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bytes), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, target.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * 4), indices.data(),
                     GL_STATIC_DRAW);
        if (surface) {
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                                  reinterpret_cast<void*>(offsetof(SurfaceVertex, fromX)));
            glEnableVertexAttribArray(1);
            for (unsigned i = 0; i < 3; ++i) {
                glVertexAttribIPointer(
                    2 + i, 1, GL_UNSIGNED_INT, sizeof(SurfaceVertex),
                    reinterpret_cast<void*>(offsetof(SurfaceVertex, layer) + i * 4));
                glEnableVertexAttribArray(2 + i);
            }
        } else {
            glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(ChunkMeshVertex), nullptr);
            glEnableVertexAttribArray(0);
        }
        target.count = std::uint32_t(indices.size());
    };
    part(m.cutout, u.mesh->cutoutVertices.data(),
         u.mesh->cutoutVertices.size() * sizeof(ChunkMeshVertex), u.mesh->cutoutIndices, false);
    part(m.plants, u.mesh->plantVertices.data(),
         u.mesh->plantVertices.size() * sizeof(SurfaceVertex), u.mesh->plantIndices, true);
    part(m.translucent, u.mesh->translucentVertices.data(),
         u.mesh->translucentVertices.size() * sizeof(SurfaceVertex), u.mesh->translucentIndices,
         true);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    if (u.mesh->surface3D)
        glBufferData(GL_ARRAY_BUFFER,
                     GLsizeiptr(u.mesh->surfaceVertices.size() * sizeof(SurfaceVertex)),
                     u.mesh->surfaceVertices.data(), GL_STATIC_DRAW);
    else if (u.mesh->heightmap)
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(u.mesh->heightVertices.size() * sizeof(HeightVertex)),
                     u.mesh->heightVertices.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(u.mesh->vertices.size() * sizeof(ChunkMeshVertex)),
                     u.mesh->vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    auto& mainIndices = u.mesh->surface3D ? u.mesh->surfaceIndices : u.mesh->indices;
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mainIndices.size() * 4), mainIndices.data(),
                 GL_STATIC_DRAW);
    if (u.mesh->surface3D) {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                              reinterpret_cast<void*>(offsetof(SurfaceVertex, fromX)));
        glEnableVertexAttribArray(1);
        for (unsigned i = 0; i < 3; ++i) {
            glVertexAttribIPointer(2 + i, 1, GL_UNSIGNED_INT, sizeof(SurfaceVertex),
                                   reinterpret_cast<void*>(offsetof(SurfaceVertex, layer) + i * 4));
            glEnableVertexAttribArray(2 + i);
        }
        glGenBuffers(1, &m.coarseEbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.coarseEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(u.mesh->coarseIndices.size() * 4),
                     u.mesh->coarseIndices.data(), GL_STATIC_DRAW);
        m.coarseCount = std::uint32_t(u.mesh->coarseIndices.size());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    } else if (u.mesh->heightmap) {
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(HeightVertex), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(HeightVertex),
                               reinterpret_cast<void*>(offsetof(HeightVertex, layer)));
        glEnableVertexAttribArray(1);
        glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(HeightVertex),
                               reinterpret_cast<void*>(offsetof(HeightVertex, face)));
        glEnableVertexAttribArray(2);
    } else {
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(ChunkMeshVertex), nullptr);
        glEnableVertexAttribArray(0);
    }
    glBindVertexArray(0);
    if (glGetError() != GL_NO_ERROR || !m.vao || !m.vbo || !m.ebo) {
        destroy(m);
        return false;
    }
    m.indexCount = static_cast<std::uint32_t>(mainIndices.size());
    m.instanceId = u.mesh->instanceId;
    m.revision = u.mesh->revision;
    m.heightmap = u.mesh->heightmap;
    m.surface3D = u.mesh->surface3D;
    m.bytes = u.mesh->byteSize();
    if (existing != pending_.end())
        destroy(existing->second);
    pending_[u.coord] = m;
    uploadedBytes_ += u.mesh->byteSize();
    uploadSpentMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStarted)
            .count();
    return true;
}
std::size_t Renderer::allocatedMeshBytes() const {
    std::size_t bytes = 0;
    for (auto& [c, m] : meshes_)
        bytes += m.bytes;
    for (auto& [c, m] : pending_)
        bytes += m.bytes;
    for (auto& r : retired_)
        bytes += r.mesh.bytes;
    return bytes;
}
bool Renderer::compactSurface(GpuMesh& mesh, const ChunkMesh& source) {
    if (mesh.compacted || !mesh.surface3D || uploadSpentMs_ >= 2)
        return false;
    std::size_t newBytes =
        source.compactVertices.size() * sizeof(SurfaceVertex) + source.compactIndices.size() * 4;
    if (allocatedMeshBytes() + newBytes > meshBudget_)
        return false;
    auto start = std::chrono::steady_clock::now();
    GpuMesh replacement;
    glGenVertexArrays(1, &replacement.vao);
    glGenBuffers(1, &replacement.vbo);
    glGenBuffers(1, &replacement.ebo);
    glBindVertexArray(replacement.vao);
    glBindBuffer(GL_ARRAY_BUFFER, replacement.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(source.compactVertices.size() * sizeof(SurfaceVertex)),
                 source.compactVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, replacement.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(source.compactIndices.size() * 4),
                 source.compactIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                          reinterpret_cast<void*>(offsetof(SurfaceVertex, fromX)));
    glEnableVertexAttribArray(1);
    for (unsigned i = 0; i < 3; ++i) {
        glVertexAttribIPointer(2 + i, 1, GL_UNSIGNED_INT, sizeof(SurfaceVertex),
                               reinterpret_cast<void*>(offsetof(SurfaceVertex, layer) + i * 4));
        glEnableVertexAttribArray(2 + i);
    }
    if (glGetError() != GL_NO_ERROR) {
        destroy(replacement);
        return false;
    }
    replacement.indexCount = std::uint32_t(source.compactIndices.size());
    replacement.compacted = true;
    replacement.surface3D = true;
    replacement.instanceId = mesh.instanceId;
    replacement.revision = mesh.revision;
    auto oldBytes = source.surfaceVertices.size() * sizeof(SurfaceVertex) +
                    (source.surfaceIndices.size() + source.coarseIndices.size()) * 4;
    replacement.bytes = mesh.bytes - oldBytes + newBytes;
    replacement.cutout = mesh.cutout;
    replacement.plants = mesh.plants;
    replacement.translucent = mesh.translucent;
    mesh.cutout = {};
    mesh.plants = {};
    mesh.translucent = {};
    mesh.bytes = oldBytes;
    retire(mesh);
    mesh = replacement;
    uploadedBytes_ += newBytes;
    uploadSpentMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return true;
}
bool Renderer::validateHeightmapForTests() const {
    assertThread();
    HeightVertex vertices[]{
        {0, 4, 0, 0, 0, 2}, {0, 4, 4, 0, 0, 2}, {8, 4, 4, 0, 0, 2}, {8, 4, 0, 0, 0, 2}};
    std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
    GLuint vao = 0, vbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(HeightVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(HeightVertex),
                           reinterpret_cast<void*>(offsetof(HeightVertex, layer)));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(HeightVertex),
                           reinterpret_cast<void*>(offsetof(HeightVertex, face)));
    glEnableVertexAttribArray(2);
    glUseProgram(heightmapProgram_);
    glViewport(0, 0, 128, 128);
    glDisable(GL_CULL_FACE);
    std::array<float, 16> projection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> view{0.25f, 0, 0, 0, 0, 0.125f, 0, 0, 0, 0.25f, 0, 0, -1, -0.5f, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(heightmapProgram_, "uView"), 1, GL_FALSE, view.data());
    glUniformMatrix4fv(glGetUniformLocation(heightmapProgram_, "uProjection"), 1, GL_FALSE,
                       projection.data());
    glUniform3f(glGetUniformLocation(heightmapProgram_, "uChunkOrigin"), 0, 0, 0);
    glUniform1i(glGetUniformLocation(heightmapProgram_, "uBlockTextures"), 0);
    glUniform1ui(glGetUniformLocation(heightmapProgram_, "uTextureSeed"), textureSeed_);
    glUniform2ui(glGetUniformLocation(heightmapProgram_, "uChunkBlockX"), 0, 0);
    glUniform2ui(glGetUniformLocation(heightmapProgram_, "uChunkBlockZ"), 0, 0);
    bool correct = true;
    for (float morph : {0.0f, 1.0f}) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniform1f(glGetUniformLocation(heightmapProgram_, "uMorph"), morph);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        unsigned char low[3]{}, high[3]{};
        glReadPixels(64, 40, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, low);
        glReadPixels(64, 104, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, high);
        bool lowHit = low[0] || low[1] || low[2], highHit = high[0] || high[1] || high[2];
        correct &= morph == 0 ? (lowHit && !highHit) : (!lowHit && highHit);
    }
    correct &= glGetError() == GL_NO_ERROR;
    glBindVertexArray(0);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glEnable(GL_CULL_FACE);
    return correct;
}
bool Renderer::validateSurfaceForTests() const {
    assertThread();
    SurfaceVertex vertices[]{{4, 4, 2, 0, 0, 0, 0, 5, 0, 0},
                             {8, 4, 2, 4, 0, 0, 0, 5, 0, 0},
                             {8, 8, 2, 4, 4, 0, 0, 5, 0, 0},
                             {4, 8, 2, 0, 4, 0, 0, 5, 0, 0}};
    std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
    GLuint vao = 0, vbo = 0, ebo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SurfaceVertex),
                          reinterpret_cast<void*>(offsetof(SurfaceVertex, fromX)));
    glEnableVertexAttribArray(1);
    for (unsigned i = 0; i < 3; ++i) {
        glVertexAttribIPointer(2 + i, 1, GL_UNSIGNED_INT, sizeof(SurfaceVertex),
                               reinterpret_cast<void*>(offsetof(SurfaceVertex, layer) + i * 4));
        glEnableVertexAttribArray(2 + i);
    }
    glUseProgram(surfaceProgram_);
    glViewport(0, 0, 128, 128);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        view{0.25f, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, 0.2f, 0, -1, -1, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(surfaceProgram_, "uView"), 1, GL_FALSE, view.data());
    glUniformMatrix4fv(glGetUniformLocation(surfaceProgram_, "uProjection"), 1, GL_FALSE,
                       identity.data());
    glUniform3f(glGetUniformLocation(surfaceProgram_, "uChunkOrigin"), 0, 0, 0);
    glUniform1i(glGetUniformLocation(surfaceProgram_, "uBlockTextures"), 0);
    glUniform1ui(glGetUniformLocation(surfaceProgram_, "uTextureSeed"), textureSeed_);
    glUniform1i(glGetUniformLocation(surfaceProgram_, "uAlphaMode"), 0);
    glUniform2ui(glGetUniformLocation(surfaceProgram_, "uChunkBlockX"), 0, 0);
    glUniform2ui(glGetUniformLocation(surfaceProgram_, "uChunkBlockZ"), 0, 0);
    bool correct = true;
    for (float morph : {0.0f, 1.0f}) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniform1f(glGetUniformLocation(surfaceProgram_, "uMorph"), morph);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        unsigned char low[3]{}, high[3]{};
        glReadPixels(16, 16, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, low);
        glReadPixels(96, 96, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, high);
        float depth = 0;
        glReadPixels(morph == 0 ? 16 : 96, morph == 0 ? 16 : 96, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                     &depth);
        bool lo = low[0] || low[1] || low[2], hi = high[0] || high[1] || high[2];
        correct &= morph == 0 ? (lo && !hi) : (!lo && hi);
        correct &= std::abs(depth - (morph == 0 ? 0.5f : 0.7f)) < 0.001f;
    }
    glUniform1f(glGetUniformLocation(surfaceProgram_, "uMorph"), 0);
    for (auto test : std::array<std::pair<unsigned, int>, 2>{{{44, 1}, {32, 2}}}) {
        for (auto& v : vertices)
            v.layer = test.first;
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniform1i(glGetUniformLocation(surfaceProgram_, "uAlphaMode"), test.second);
        if (test.second == 2) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        std::array<unsigned char, 64 * 64 * 3> pixels{};
        glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        unsigned visible = 0;
        for (unsigned i = 0; i < pixels.size(); i += 3)
            visible += bool(pixels[i] || pixels[i + 1] || pixels[i + 2]);
        if (test.second == 1)
            correct &= visible > 0 && visible < 4096;
        else {
            float depth = 0;
            glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            correct &= visible > 0 && std::abs(depth - 1) < 0.001f;
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    correct &= glGetError() == GL_NO_ERROR;
    glBindVertexArray(0);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glEnable(GL_CULL_FACE);
    return correct;
}
void Renderer::setFrameId(std::uint64_t id) {
    assertThread();
    frameId_ = id;
    auto slot = std::size_t(id % 8);
    frameSlot_ = -1;
    if (framePending_[slot]) {
        GLint ready = 0;
        glGetQueryObjectiv(frameQueries_[slot * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 start = 0, end = 0;
            glGetQueryObjectui64v(frameQueries_[slot * 2], GL_QUERY_RESULT, &start);
            glGetQueryObjectui64v(frameQueries_[slot * 2 + 1], GL_QUERY_RESULT, &end);
            gpuFrameMs_ = double(end - start) / 1e6;
            gpuFrameTimingId_ = frameQueryIds_[slot];
            framePending_[slot] = false;
        }
    }
    if (!framePending_[slot]) {
        glQueryCounter(frameQueries_[slot * 2], GL_TIMESTAMP);
        frameSlot_ = int(slot);
    }
}
void Renderer::finishFrame() {
    assertThread();
    if (frameSlot_ < 0)
        return;
    auto slot = std::size_t(frameSlot_);
    glQueryCounter(frameQueries_[slot * 2 + 1], GL_TIMESTAMP);
    framePending_[slot] = true;
    frameQueryIds_[slot] = frameId_;
    frameSlot_ = -1;
}
bool Renderer::publish(const UploadGroup& group) {
    assertThread();
    for (auto& u : group.members) {
        auto it = pending_.find(u.coord);
        if (it == pending_.end() || it->second.instanceId != u.mesh->instanceId ||
            it->second.revision != u.mesh->revision)
            return false;
    }
    for (auto& u : group.members) {
        auto& active = meshes_[u.coord];
        retire(active);
        active = pending_.at(u.coord);
        pending_.erase(u.coord);
    }
    ++swaps_;
    return true;
}
void Renderer::synchronize(const std::vector<ChunkCoord>& loaded) {
    assertThread();
    uploadSpentMs_ = 0;
    std::unordered_set<ChunkCoord, ChunkCoordHash> keep(loaded.begin(), loaded.end());
    for (auto it = meshes_.begin(); it != meshes_.end();)
        if (!keep.count(it->first)) {
            retire(it->second);
            it = meshes_.erase(it);
        } else
            ++it;
    for (auto it = pending_.begin(); it != pending_.end();)
        if (!keep.count(it->first)) {
            destroy(it->second);
            it = pending_.erase(it);
        } else
            ++it;
    collectRetired();
}
bool Renderer::validatePublishedForTests(const std::vector<RenderChunk>& chunks) const {
    assertThread();
    for (auto& c : chunks) {
        auto it = meshes_.find(c.coord);
        if (it == meshes_.end() || it->second.instanceId != c.mesh->instanceId ||
            it->second.revision != c.mesh->revision || it->second.heightmap != c.mesh->heightmap ||
            it->second.surface3D != c.mesh->surface3D)
            return false;
    }
    return true;
}
void Renderer::render(const Camera& camera, const std::vector<RenderChunk>& chunks, int w, int h,
                      bool underwater) {
    assertThread();
    drawCalls_ = 0;
    budgetPressure_ = false;
    if (w <= 0 || h <= 0)
        return;
    glViewport(0, 0, w, h);
    glClearColor(0.48f, 0.73f, 0.92f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(program_, "uAlphaMode"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_);
    glUniform1i(glGetUniformLocation(program_, "uBlockTextures"), 0);
    glUniform1ui(glGetUniformLocation(program_, "uTextureSeed"), textureSeed_);
    auto view = camera.viewMatrix(),
         projection = camera.projectionMatrix(static_cast<float>(w) / h);
    glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, view.data());
    glUniformMatrix4fv(glGetUniformLocation(program_, "uProjection"), 1, GL_FALSE,
                       projection.data());
    auto q = queryCursor_ % 8;
    bool timed = false;
    if (queryPending_[q]) {
        GLint ready = 0;
        glGetQueryObjectiv(queries_[q * 4 + 3], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            std::array<GLuint64, 4> ns{};
            for (unsigned i = 0; i < 4; ++i)
                glGetQueryObjectui64v(queries_[q * 4 + i], GL_QUERY_RESULT, &ns[i]);
            gpuTerrainMs_ = double(ns[3] - ns[0]) / 1e6;
            for (unsigned i = 0; i < 3; ++i)
                gpuPassMs_[i] = double(ns[i + 1] - ns[i]) / 1e6;
            gpuTimingFrame_ = queryFrame_[q];
            queryPending_[q] = false;
        }
    }
    if (!queryPending_[q]) {
        glQueryCounter(queries_[q * 4], GL_TIMESTAMP);
        timed = true;
    }
    struct Transparent {
        const RenderChunk* chunk;
        GpuMesh* mesh;
        const SurfaceFace* face;
        float depth;
    };
    std::vector<Transparent> transparent;
    struct Cutout {
        const RenderChunk* chunk;
        GpuMesh* mesh;
    };
    std::vector<Cutout> cutouts;
    auto uniforms = [&](GLuint p, ChunkCoord coord, float morph, int alpha) {
        glUseProgram(p);
        glUniform1i(glGetUniformLocation(p, "uBlockTextures"), 0);
        glUniform1ui(glGetUniformLocation(p, "uTextureSeed"), textureSeed_);
        glUniformMatrix4fv(glGetUniformLocation(p, "uView"), 1, GL_FALSE, view.data());
        glUniformMatrix4fv(glGetUniformLocation(p, "uProjection"), 1, GL_FALSE, projection.data());
        glUniform1f(glGetUniformLocation(p, "uMorph"), morph);
        glUniform1i(glGetUniformLocation(p, "uAlphaMode"), alpha);
        glUniform1i(glGetUniformLocation(p, "uUnderwater"), underwater ? 1 : 0);
        auto eye = camera.position();
        glUniform3f(glGetUniformLocation(p, "uEyePosition"), eye[0], eye[1], eye[2]);
        static const auto animationStart = std::chrono::steady_clock::now();
        glUniform1f(
            glGetUniformLocation(p, "uTime"),
            float(std::chrono::duration<double>(std::chrono::steady_clock::now() - animationStart)
                      .count()));
        glUniform3f(glGetUniformLocation(p, "uChunkOrigin"),
                    float((coord.x - camera.origin().x) * 16), 0,
                    float((coord.z - camera.origin().z) * 16));
        auto x = std::uint64_t(coord.x * 16), z = std::uint64_t(coord.z * 16);
        glUniform2ui(glGetUniformLocation(p, "uChunkBlockX"), GLuint(x), GLuint(x >> 32));
        glUniform2ui(glGetUniformLocation(p, "uChunkBlockZ"), GLuint(z), GLuint(z >> 32));
    };
    for (auto& c : chunks) {
        auto it = meshes_.find(c.coord);
        if (it == meshes_.end() || it->second.instanceId != c.mesh->instanceId ||
            it->second.revision != c.mesh->revision)
            continue;
        if (c.morph >= 1 && c.mesh->surface3D && !it->second.compacted && uploadSpentMs_ < 2)
            compactSurface(it->second, *c.mesh);
        // Conservative homogeneous AABB frustum test, relative to the floating origin.
        float ox = float((c.coord.x - camera.origin().x) * 16),
              oz = float((c.coord.z - camera.origin().z) * 16);
        std::array<Clip, 8> corners{};
        for (int i = 0; i < 8; ++i)
            corners[i] = transform(
                projection, transform(view, {ox + ((i & 1) ? 16.0f : 0.0f), (i & 2) ? 256.0f : 0.0f,
                                             oz + ((i & 4) ? 16.0f : 0.0f), 1}));
        bool culled = false;
        for (int axis = 0; axis < 3; ++axis)
            for (float sign : {-1.0f, 1.0f}) {
                bool outside = true;
                for (auto& corner : corners)
                    outside &= sign * corner[axis] > corner[3];
                culled |= outside;
            }
        if (culled)
            continue;
        auto selected = it->second.surface3D   ? surfaceProgram_
                        : it->second.heightmap ? heightmapProgram_
                                               : program_;
        glUseProgram(selected);
        glUniform1i(glGetUniformLocation(selected, "uAlphaMode"), 0);
        glUniform1i(glGetUniformLocation(selected, "uUnderwater"), underwater ? 1 : 0);
        auto eye = camera.position();
        glUniform3f(glGetUniformLocation(selected, "uEyePosition"), eye[0], eye[1], eye[2]);
        glUniform1i(glGetUniformLocation(selected, "uBlockTextures"), 0);
        glUniform1ui(glGetUniformLocation(selected, "uTextureSeed"), textureSeed_);
        glUniformMatrix4fv(glGetUniformLocation(selected, "uView"), 1, GL_FALSE, view.data());
        glUniformMatrix4fv(glGetUniformLocation(selected, "uProjection"), 1, GL_FALSE,
                           projection.data());
        if (it->second.heightmap || it->second.surface3D)
            glUniform1f(glGetUniformLocation(selected, "uMorph"), c.morph);
        glUniform3f(glGetUniformLocation(selected, "uChunkOrigin"),
                    static_cast<float>((c.coord.x - camera.origin().x) * 16), 0,
                    static_cast<float>((c.coord.z - camera.origin().z) * 16));
        auto bx = static_cast<std::uint64_t>(c.coord.x * 16),
             bz = static_cast<std::uint64_t>(c.coord.z * 16);
        glUniform2ui(glGetUniformLocation(selected, "uChunkBlockX"), static_cast<GLuint>(bx),
                     static_cast<GLuint>(bx >> 32));
        glUniform2ui(glGetUniformLocation(selected, "uChunkBlockZ"), static_cast<GLuint>(bz),
                     static_cast<GLuint>(bz >> 32));
        if (it->second.indexCount) {
            glBindVertexArray(it->second.vao);
            bool coarse = it->second.surface3D && c.morph >= 1 && !it->second.compacted;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, coarse ? it->second.coarseEbo : it->second.ebo);
            glDrawElements(GL_TRIANGLES, coarse ? it->second.coarseCount : it->second.indexCount,
                           GL_UNSIGNED_INT, nullptr);
            ++drawCalls_;
        }
        if (it->second.cutout.count || it->second.plants.count)
            cutouts.push_back({&c, &it->second});
        auto forward = camera.forward(), position = camera.position();
        for (auto& face : c.mesh->translucentFaces) {
            float dx = ox + face.x - position[0], dy = face.y - position[1],
                  dz = oz + face.z - position[2];
            transparent.push_back(
                {&c, &it->second, &face, dx * forward[0] + dy * forward[1] + dz * forward[2]});
        }
    }
    if (timed)
        glQueryCounter(queries_[q * 4 + 1], GL_TIMESTAMP);
    for (auto& item : cutouts) {
        auto& c = *item.chunk;
        auto& m = *item.mesh;
        if (m.cutout.count) {
            uniforms(program_, c.coord, c.morph, 1);
            glBindVertexArray(m.cutout.vao);
            glDrawElements(GL_TRIANGLES, m.cutout.count, GL_UNSIGNED_INT, nullptr);
            ++drawCalls_;
        }
        if (m.plants.count) {
            uniforms(surfaceProgram_, c.coord, c.morph, 1);
            glDisable(GL_CULL_FACE);
            glBindVertexArray(m.plants.vao);
            glDrawElements(GL_TRIANGLES, m.plants.count, GL_UNSIGNED_INT, nullptr);
            glEnable(GL_CULL_FACE);
            ++drawCalls_;
        }
    }
    if (timed)
        glQueryCounter(queries_[q * 4 + 2], GL_TIMESTAMP);
    std::stable_sort(transparent.begin(), transparent.end(),
                     [](auto& a, auto& b) { return a.depth > b.depth; });
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (std::size_t i = 0; i < transparent.size();) {
        auto* mesh = transparent[i].mesh;
        auto* chunk = transparent[i].chunk;
        std::vector<std::uint32_t> indices;
        do {
            auto& item = transparent[i];
            auto first = item.face->first;
            indices.insert(indices.end(), chunk->mesh->translucentIndices.begin() + first,
                           chunk->mesh->translucentIndices.begin() + first + item.face->count);
            ++i;
        } while (i < transparent.size() && transparent[i].mesh == mesh);
        uniforms(surfaceProgram_, chunk->coord, chunk->morph, 2);
        glBindVertexArray(mesh->translucent.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->translucent.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * 4), indices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawElements(GL_TRIANGLES, GLsizei(indices.size()), GL_UNSIGNED_INT, nullptr);
        ++drawCalls_;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    if (timed) {
        glQueryCounter(queries_[q * 4 + 3], GL_TIMESTAMP);
        queryPending_[q] = true;
        queryFrame_[q] = frameId_;
        ++queryCursor_;
    }
}
void Renderer::renderTarget(const Camera& camera, const BlockTarget& target, int w, int h,
                            bool crosshair) {
    assertThread();
    if (w <= 0 || h <= 0)
        return;
    glUseProgram(overlayProgram_);
    glBindVertexArray(overlayVao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    std::vector<Clip> vertices;
    if (target.hit) {
        auto view = camera.viewMatrix(), proj = camera.projectionMatrix(static_cast<float>(w) / h);
        std::array<Clip, 8> corners{};
        for (int i = 0; i < 8; ++i) {
            bool flower = isFlower(target.type);
            float min = flower ? 0.148f : -0.002f, max = flower ? 0.852f : 1.002f,
                  top = flower ? 0.902f : 1.002f;
            Clip pos{
                static_cast<float>(target.block.x - camera.origin().x * 16) + ((i & 1) ? max : min),
                target.block.y + ((i & 2) ? top : -0.002f),
                static_cast<float>(target.block.z - camera.origin().z * 16) + ((i & 4) ? max : min),
                1};
            corners[i] = transform(proj, transform(view, pos));
        }
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit))
                    ribbon(vertices, corners[i], corners[i | bit], w, h, 1.5f);
        glUniform3f(glGetUniformLocation(overlayProgram_, "color"), 0, 0, 0);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Clip), vertices.data(),
                     GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    }
    if (crosshair) {
        vertices.clear();
        glDisable(GL_DEPTH_TEST);
        ribbon(vertices, {-8.0f / w, 0, 0, 1}, {8.0f / w, 0, 0, 1}, w, h, 1.5f);
        ribbon(vertices, {0, -8.0f / h, 0, 1}, {0, 8.0f / h, 0, 1}, w, h, 1.5f);
        glUniform3f(glGetUniformLocation(overlayProgram_, "color"), 0.95f, 0.95f, 0.95f);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Clip), vertices.data(),
                     GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
} // namespace bloxide
