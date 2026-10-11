#pragma once
#include <array>
#include <functional>
#include <bloxide/Block.hpp>
namespace bloxide {
struct PhysicsSettings {
    float walkSpeed=5,groundAcceleration=45,airAcceleration=12;
    float gravity=24,terminalSpeed=40,jumpSpeed=7,airDrag=0.15f;
    float grassFriction=14,dirtFriction=12,bedrockFriction=11,stoneFriction=9;
    float stopSpeed=0.01f,maxSubstep=0.005f;
    float friction(BlockType material) const;
};
class PlayerPhysics {
public:
    using SolidQuery = std::function<bool(int, int, int)>;
    using MaterialQuery = std::function<BlockType(int,int,int)>;
    PhysicsSettings settings{};
    std::array<float, 3> feet{8.0f, 64.0f, 8.0f};
    std::array<float, 3> velocity{};
    bool grounded = false;
    static constexpr float Radius = 0.3f;
    static constexpr float Height = 1.8f;
    static constexpr float EyeHeight = 1.62f;
    void step(float dt, float moveX, float moveZ, bool jump, const SolidQuery& solid,
              const MaterialQuery& material={});
    bool overlapsBlock(int x, int y, int z) const;
    std::array<float, 3> eye() const { return {feet[0], feet[1] + EyeHeight, feet[2]}; }
private:
    void moveAxis(int axis, float delta, const SolidQuery& solid);
    float supportFriction(const SolidQuery& solid,const MaterialQuery& material) const;
};
}
