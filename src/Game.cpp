#include "bloxide/Renderer.hpp"
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <sstream>

#ifndef BLOXIDE_SOURCE_DIR
#define BLOXIDE_SOURCE_DIR "."
#endif

namespace bloxide {
class Game {
public:
    int run();
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
    float velocityY_ = 0.0f;
};

int Game::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window_ = SDL_CreateWindow("Bloxide v1.1.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) { SDL_Quit(); return 1; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) return 1;
    SDL_GL_SetSwapInterval(1);
    if (!renderer_.initialize(std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.vert", std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.frag")) return 1;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    camera_.setPosition({8.0f, 67.5f, 8.0f});
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
        SDL_GL_SwapWindow(window_);
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

void Game::event(const SDL_Event& e) {
    if (e.type == SDL_QUIT) running_ = false;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F3 && e.key.repeat == 0) hudEnabled_ = !hudEnabled_;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.repeat == 0) { captured_ = !captured_; SDL_SetRelativeMouseMode(captured_ ? SDL_TRUE : SDL_FALSE); }
    if (e.type == SDL_MOUSEMOTION && captured_) camera_.rotate(static_cast<float>(e.motion.xrel) * 0.1f, -static_cast<float>(e.motion.yrel) * 0.1f);
    if (e.type == SDL_MOUSEBUTTONDOWN && captured_) { if (e.button.button == SDL_BUTTON_LEFT) interact(false); if (e.button.button == SDL_BUTTON_RIGHT) interact(true); }
}

void Game::update(float dt) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const auto f = camera_.forward();
    const auto r = camera_.right();
    std::array<float, 3> move{0.0f, 0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W]) { move[0] += f[0]; move[2] += f[2]; }
    if (keys[SDL_SCANCODE_S]) { move[0] -= f[0]; move[2] -= f[2]; }
    if (keys[SDL_SCANCODE_D]) { move[0] += r[0]; move[2] += r[2]; }
    if (keys[SDL_SCANCODE_A]) { move[0] -= r[0]; move[2] -= r[2]; }
    const float length = std::sqrt(move[0] * move[0] + move[2] * move[2]);
    if (length > 0.0f) camera_.move({move[0] / length * 5.0f * dt, 0.0f, move[2] / length * 5.0f * dt});
    const auto p = camera_.position();
    constexpr float ground = 67.5f;
    if (p[1] <= ground + 0.02f) { camera_.setPosition({p[0], ground, p[2]}); if (keys[SDL_SCANCODE_SPACE]) velocityY_ = 5.5f; }
    velocityY_ -= 14.0f * dt;
    auto next = camera_.position(); next[1] += velocityY_ * dt;
    if (next[1] < ground) { next[1] = ground; velocityY_ = 0.0f; }
    camera_.setPosition(next);
}

void Game::interact(bool place) {
    const auto hit = world_.raycast(camera_.position(), camera_.forward(), 8.0f);
    if (!hit.hit) return;
    const Int3 target = place ? hit.previous : hit.block;
    world_.set(target.x, target.y, target.z, place ? BlockType::Grass : BlockType::Air);
}
} // namespace bloxide

int main() { bloxide::Game game; return game.run(); }
