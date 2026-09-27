#include "bloxide/Renderer.hpp"

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#ifndef BLOXIDE_SOURCE_DIR
#define BLOXIDE_SOURCE_DIR "."
#endif

extern "C" int gladLoadGLLoader(void* (*load)(const char*));

namespace bloxide {

class Game {
public:
    int run();

private:
    void handleEvent(const SDL_Event& event);
    void update(float deltaSeconds);
    void interact(bool place);

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    World world_;
    Camera camera_;
    Renderer renderer_;
    bool running_ = true;
    bool mouseCaptured_ = true;
    float verticalVelocity_ = 0.0f;
    bool grounded_ = false;
};

int Game::run() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window_ = SDL_CreateWindow("Bloxide v1.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) { SDL_Quit(); return 1; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_ || !gladLoadGLLoader(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress))) return 1;
    SDL_GL_SetSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    if (!renderer_.initialize(std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.vert", std::string(BLOXIDE_SOURCE_DIR) + "/shaders/voxel.frag")) return 1;
    renderer_.rebuildMesh(world_);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    std::uint64_t previous = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) handleEvent(event);
        const std::uint64_t now = SDL_GetPerformanceCounter();
        const float delta = std::min(0.1f, static_cast<float>(now - previous) / static_cast<float>(SDL_GetPerformanceFrequency()));
        previous = now;
        update(delta);
        int width = 0, height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        renderer_.render(camera_, width, height);
        SDL_GL_SwapWindow(window_);
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_GL_DeleteContext(context_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return 0;
}

void Game::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) running_ = false;
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && event.key.repeat == 0) {
        mouseCaptured_ = !mouseCaptured_;
        SDL_SetRelativeMouseMode(mouseCaptured_ ? SDL_TRUE : SDL_FALSE);
    }
    if (event.type == SDL_MOUSEMOTION && mouseCaptured_) camera_.rotate(static_cast<float>(event.motion.xrel) * 0.10f, -static_cast<float>(event.motion.yrel) * 0.10f);
    if (event.type == SDL_MOUSEBUTTONDOWN && mouseCaptured_) {
        if (event.button.button == SDL_BUTTON_LEFT) interact(false);
        if (event.button.button == SDL_BUTTON_RIGHT) interact(true);
    }
}

void Game::update(float deltaSeconds) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const auto forward = camera_.forward();
    const auto right = camera_.right();
    std::array<float, 3> movement{0.0f, 0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W]) { movement[0] += forward[0]; movement[2] += forward[2]; }
    if (keys[SDL_SCANCODE_S]) { movement[0] -= forward[0]; movement[2] -= forward[2]; }
    if (keys[SDL_SCANCODE_D]) { movement[0] += right[0]; movement[2] += right[2]; }
    if (keys[SDL_SCANCODE_A]) { movement[0] -= right[0]; movement[2] -= right[2]; }
    const float movementLength = std::sqrt(movement[0] * movement[0] + movement[2] * movement[2]);
    if (movementLength > 0.0f) {
        movement[0] /= movementLength;
        movement[2] /= movementLength;
        camera_.move({movement[0] * 5.0f * deltaSeconds, 0.0f, movement[2] * 5.0f * deltaSeconds});
    }
    const auto pos = camera_.position();
    const float ground = 3.65f;
    grounded_ = pos[1] <= ground + 0.02f;
    if (grounded_) {
        camera_.setPosition({pos[0], ground, pos[2]});
        if (keys[SDL_SCANCODE_SPACE]) verticalVelocity_ = 5.5f;
    }
    verticalVelocity_ -= 14.0f * deltaSeconds;
    auto next = camera_.position();
    next[1] += verticalVelocity_ * deltaSeconds;
    if (next[1] < ground) { next[1] = ground; verticalVelocity_ = 0.0f; }
    camera_.setPosition(next);
}

void Game::interact(bool place) {
    const auto hit = world_.raycast(camera_.position(), camera_.forward(), 8.0f);
    if (!hit.hit) return;
    const Int3 target = place ? hit.previous : hit.block;
    if (place && target.y <= 2) return;
    if (world_.set(target.x, target.y, target.z, place ? BlockType::Grass : BlockType::Air)) renderer_.rebuildMesh(world_);
}

} // namespace bloxide

int main() {
    bloxide::Game game;
    return game.run();
}
