#include <bloxide/Physics.hpp>
#include <algorithm>
#include <cmath>
namespace bloxide {
bool PlayerPhysics::overlapsBlock(int x, int y, int z) const {
    return feet[0] + Radius > x && feet[0] - Radius < x + 1 &&
           feet[1] + Height > y && feet[1] < y + 1 &&
           feet[2] + Radius > z && feet[2] - Radius < z + 1;
}
void PlayerPhysics::moveAxis(int axis, float delta, const SolidQuery& solid) {
    if (delta == 0.0f) return;
    auto target = feet;
    target[axis] += delta;
    std::array<float, 3> lo{target[0] - Radius, target[1], target[2] - Radius};
    std::array<float, 3> hi{target[0] + Radius, target[1] + Height, target[2] + Radius};
    float allowed = delta;
    constexpr float epsilon = 0.00001f;
    for (int y = static_cast<int>(std::floor(lo[1] + epsilon)); y <= static_cast<int>(std::floor(hi[1] - epsilon)); ++y)
        for (int z = static_cast<int>(std::floor(lo[2] + epsilon)); z <= static_cast<int>(std::floor(hi[2] - epsilon)); ++z)
            for (int x = static_cast<int>(std::floor(lo[0] + epsilon)); x <= static_cast<int>(std::floor(hi[0] - epsilon)); ++x) {
                if (!solid(x, y, z)) continue;
                const int block[3]{x,y,z};
                const float extent = axis == 1 ? Height : Radius;
                if (delta > 0.0f) allowed = std::min(allowed, block[axis] - feet[axis] - extent);
                else allowed = std::max(allowed, block[axis] + 1.0f - feet[axis] + (axis == 1 ? 0.0f : Radius));
            }
    feet[axis] += allowed;
    if (allowed != delta) {
        velocity[axis] = 0.0f;
        if (axis == 1 && delta < 0.0f) grounded = true;
    }
}
void PlayerPhysics::step(float dt, float moveX, float moveZ, bool jump, const SolidQuery& solid) {
    if (dt <= 0.0f) return;
    const float length = std::sqrt(moveX*moveX + moveZ*moveZ);
    velocity[0] = moveX / std::max(1.0f, length) * 5.0f;
    velocity[2] = moveZ / std::max(1.0f, length) * 5.0f;
    if (jump && grounded) { velocity[1] = 7.0f; grounded = false; }
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.005f)));
    const float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        velocity[1] = std::max(-40.0f, velocity[1] - 24.0f * h);
        grounded = false;
        moveAxis(0, velocity[0] * h, solid);
        moveAxis(2, velocity[2] * h, solid);
        moveAxis(1, velocity[1] * h, solid);
    }
}
}
