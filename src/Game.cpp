#include "bloxide/Renderer.hpp"
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <sstream>
#include <bloxide/Physics.hpp>
#include <iostream>
#include <vector>

#ifndef BLOXIDE_SOURCE_DIR
#define BLOXIDE_SOURCE_DIR "."
#endif

namespace bloxide {
class Game {
public:
    int run(bool smokeTest = false);
private:
    void event(const SDL_Event& e);
    void update(float dt);
    void interact(bool place);
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    World world_;
    Camera camera_;
    Renderer renderer_;
    bool running_ = true;
    bool captured_ = true;
    bool hudEnabled_ = false;
    PlayerPhysics player_;
    bool spawned_ = false;
    bool jumpRequested_ = false;
};

int Game::run(bool smokeTest) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << SDL_GetError() << std::endl; return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window_ = SDL_CreateWindow("Bloxide v1.1.5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : 0));
    if (!window_) { std::cerr << SDL_GetError() << std::endl; SDL_Quit(); return 1; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "OpenGL context/loader failed: " << SDL_GetError() << std::endl;
        if (context_) SDL_GL_DeleteContext(context_);
        SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    SDL_GL_SetSwapInterval(1);
    if (!renderer_.initialize(std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.vert", std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.frag")) {
        std::cerr << "Shader initialization failed" << std::endl;
        renderer_.shutdown(); SDL_GL_DeleteContext(context_); SDL_DestroyWindow(window_); SDL_Quit(); return 1;
    }
    SDL_SetRelativeMouseMode(smokeTest ? SDL_FALSE : SDL_TRUE);
    camera_.setPosition(player_.eye());
    int smokeFrames = 0;
    bool smokePassed = false;
    std::uint64_t previous = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) event(e);
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const float dt = std::min(0.1f, static_cast<float>(now - previous) / static_cast<float>(SDL_GetPerformanceFrequency()));
        previous = now;
        world_.update(camera_.position(), camera_.forward());
        for (const auto& upload : world_.takePendingUploads()) {
            if (renderer_.upload(upload)) world_.markUploaded(upload.coord, upload.mesh ? upload.mesh->sourceVersion : 0);
        }
        update(dt);
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        renderer_.render(camera_, world_.renderChunks(camera_.position(), camera_.forward()), width, height);
        if (smokeTest) {
            if (spawned_ && renderer_.drawCalls() > 0) {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3));
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
                std::size_t terrainPixels = 0;
                for (std::size_t p = 0; p < pixels.size(); p += 3)
                    if (pixels[p] < 120 && pixels[p + 1] > pixels[p + 2] + 20) ++terrainPixels;
                if (terrainPixels > 1000) {
                    smokePassed = true;
                    std::cout << "terrain_pixels=" << terrainPixels << " draws=" << renderer_.drawCalls()
                              << " feet_y=" << player_.feet[1] << " grounded=" << player_.grounded << std::endl;
                    running_ = false;
                }
            }
            if (++smokeFrames > 3000) running_ = false;
            SDL_Delay(1);
        }
        SDL_GL_SwapWindow(window_);
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    renderer_.shutdown();
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return smokeTest && !smokePassed ? 2 : 0;
}

void Game::event(const SDL_Event& e) {
    if (e.type == SDL_QUIT) running_ = false;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE && e.key.repeat == 0) jumpRequested_ = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F3 && e.key.repeat == 0) hudEnabled_ = !hudEnabled_;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.repeat == 0) { captured_ = !captured_; SDL_SetRelativeMouseMode(captured_ ? SDL_TRUE : SDL_FALSE); }
    if (e.type == SDL_MOUSEMOTION && captured_) camera_.rotate(static_cast<float>(e.motion.xrel) * 0.1f, -static_cast<float>(e.motion.yrel) * 0.1f);
    if (e.type == SDL_MOUSEBUTTONDOWN && captured_) { if (e.button.button == SDL_BUTTON_LEFT) interact(false); if (e.button.button == SDL_BUTTON_RIGHT) interact(true); }
}

void Game::update(float dt) {
    if (!spawned_) {
        const int x = static_cast<int>(std::floor(player_.feet[0]));
        const int z = static_cast<int>(std::floor(player_.feet[2]));
        if (!world_.columnLoaded(x, z)) return;
        for (int y = ChunkData::Height - 1; y >= 0; --y) {
            if (isSolid(world_.get(x, y, z)) && !isSolid(world_.get(x, y + 1, z)) && !isSolid(world_.get(x, y + 2, z))) {
                player_.feet[1] = static_cast<float>(y + 1);
                player_.grounded = true;
                spawned_ = true;
                break;
            }
        }
        if (!spawned_) return;
    }
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const auto f = camera_.forward();
    const auto r = camera_.right();
    std::array<float, 3> move{0.0f, 0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W]) { move[0] += f[0]; move[2] += f[2]; }
    if (keys[SDL_SCANCODE_S]) { move[0] -= f[0]; move[2] -= f[2]; }
    if (keys[SDL_SCANCODE_D]) { move[0] += r[0]; move[2] += r[2]; }
    if (keys[SDL_SCANCODE_A]) { move[0] -= r[0]; move[2] -= r[2]; }
    const float length = std::sqrt(move[0] * move[0] + move[2] * move[2]);
    if (!captured_) move = {0.0f, 0.0f, 0.0f};
    if (length > 0.0f) { move[0] /= length; move[2] /= length; }
    player_.step(dt, move[0], move[2], jumpRequested_ && captured_, [this](int x, int y, int z) {
        // Unknown chunks are barriers until their collision data arrives.
        return !world_.columnLoaded(x, z) || isSolid(world_.get(x, y, z));
    });
    jumpRequested_ = false;
    camera_.setPosition(player_.eye());
    if (hudEnabled_) {
        const auto stats = world_.stats();
        std::ostringstream title;
        title << "Bloxide v1.1.5 | loaded " << stats.loaded << " draws " << renderer_.drawCalls()
              << " feet Y " << player_.feet[1] << " grounded " << player_.grounded;
        SDL_SetWindowTitle(window_, title.str().c_str());
    }
}

void Game::interact(bool place) {
    if (!spawned_) return;
    const auto hit = world_.raycast(camera_.position(), camera_.forward(), 8.0f);
    if (!hit.hit) return;
    const Int3 target = place ? hit.previous : hit.block;
    if (place && player_.overlapsBlock(target.x, target.y, target.z)) return;
    world_.set(target.x, target.y, target.z, place ? BlockType::Grass : BlockType::Air);
}
} // namespace bloxide

int main(int argc, char** argv) {
    bloxide::Game game;
    return game.run(argc > 1 && std::string(argv[1]) == "--smoke-test");
}
