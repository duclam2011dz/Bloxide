#pragma once
#include <array>
#include <functional>
#include <bloxide/Block.hpp>
#include <bloxide/Coordinates.hpp>
namespace bloxide {
struct PhysicsSettings {
    float walkSpeed=5,groundAcceleration=45,airAcceleration=12;
    float gravity=24,terminalSpeed=40,jumpSpeed=7,airDrag=0.15f;
    float grassFriction=14,dirtFriction=12,bedrockFriction=11,stoneFriction=9;
    float stopSpeed=0.01f,maxSubstep=0.005f;
    float swimSpeed=2.8f,swimAcceleration=15,waterDrag=4,waterTerminalSpeed=8,swimVerticalSpeed=2.5f;
    float friction(BlockType material) const;
};
class PlayerPhysics {
public:
    using SolidQuery = std::function<bool(std::int64_t,int,std::int64_t)>;
    using MaterialQuery = std::function<BlockType(std::int64_t,int,std::int64_t)>;
    using FluidQuery=std::function<BlockState(std::int64_t,int,std::int64_t)>;
    PhysicsSettings settings{};
    std::array<double, 3> feet{8,80,8};
    std::array<double, 3> velocity{};
    ChunkCoord origin{};
    bool grounded = false;
    double submerged=0;
    static constexpr float Radius = 0.3f;
    static constexpr float Height = 1.8f;
    static constexpr float EyeHeight = 1.62f;
    void step(float dt, float moveX, float moveZ, bool jump, const SolidQuery& solid,
              const MaterialQuery& material={},const FluidQuery& fluid={},bool swimDown=false);
    double submergedFraction(const FluidQuery&)const;
    bool overlapsBlock(std::int64_t x,int y,std::int64_t z)const;
    bool clear(const SolidQuery& solid)const;
    bool recoverOverlap(const SolidQuery& solid);
    void rebase();
    WorldPosition worldEye()const{return {origin,feet[0],feet[1]+EyeHeight,feet[2]};}
    WorldPosition worldFeet()const{return {origin,feet[0],feet[1],feet[2]};}
    std::array<float,3> eye()const{return {static_cast<float>(feet[0]),static_cast<float>(feet[1]+EyeHeight),static_cast<float>(feet[2])};}
private:
    void moveAxis(int axis,double delta,const SolidQuery& solid);
    float supportFriction(const SolidQuery& solid,const MaterialQuery& material) const;
};
}
