#pragma once
#include <array>
#include <functional>
namespace bloxide {
class PlayerPhysics {
public:
    using SolidQuery = std::function<bool(int, int, int)>;
    std::array<float, 3> feet{8.0f, 64.0f, 8.0f};
    std::array<float, 3> velocity{};
    bool grounded = false;
    static constexpr float Radius = 0.3f;
    static constexpr float Height = 1.8f;
    static constexpr float EyeHeight = 1.62f;
    void step(float dt, float moveX, float moveZ, bool jump, const SolidQuery& solid);
    bool overlapsBlock(int x, int y, int z) const;
    std::array<float, 3> eye() const { return {feet[0], feet[1] + EyeHeight, feet[2]}; }
private:
    void moveAxis(int axis, float delta, const SolidQuery& solid);
};
}
