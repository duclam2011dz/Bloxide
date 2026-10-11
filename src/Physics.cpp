#include <bloxide/Physics.hpp>
#include <algorithm>
#include <cmath>
namespace bloxide {
float PhysicsSettings::friction(BlockType block)const {
    switch(block){
    case BlockType::Grass:return grassFriction;
    case BlockType::Dirt:return dirtFriction;
    case BlockType::Bedrock:return bedrockFriction;
    default:return stoneFriction;
    }
}
float PlayerPhysics::supportFriction(const SolidQuery& solid,const MaterialQuery& material)const {
    float coefficient=0;
    const int y=static_cast<int>(std::floor(feet[1]-0.001f));
    for(int x=static_cast<int>(std::floor(feet[0]-Radius+0.00001f));x<=static_cast<int>(std::floor(feet[0]+Radius-0.00001f));++x)
        for(int z=static_cast<int>(std::floor(feet[2]-Radius+0.00001f));z<=static_cast<int>(std::floor(feet[2]+Radius-0.00001f));++z)
            if(solid(x,y,z))coefficient=std::max(coefficient,settings.friction(material?material(x,y,z):BlockType::Stone));
    return coefficient;
}
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
void PlayerPhysics::step(float dt, float moveX, float moveZ, bool jump, const SolidQuery& solid,const MaterialQuery& material) {
    if (dt <= 0.0f || !std::isfinite(dt)) return;
    if(!std::isfinite(moveX)||!std::isfinite(moveZ)){moveX=0;moveZ=0;}
    const float length = std::sqrt(moveX*moveX + moveZ*moveZ);
    const float wishX=moveX/std::max(1.0f,length)*settings.walkSpeed;
    const float wishZ=moveZ/std::max(1.0f,length)*settings.walkSpeed;
    if (jump && grounded) { velocity[1] = settings.jumpSpeed; grounded = false; }
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / settings.maxSubstep)));
    const float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        if(!grounded){
            const float drag=std::exp(-settings.airDrag*h);
            velocity[0]*=drag;velocity[2]*=drag;
        }
        if(length>0){
            const float dx=wishX-velocity[0],dz=wishZ-velocity[2];
            const float error=std::sqrt(dx*dx+dz*dz);
            const float limit=(grounded?settings.groundAcceleration:settings.airAcceleration)*h;
            const float factor=error>0?std::min(1.0f,limit/error):0;
            velocity[0]+=dx*factor;velocity[2]+=dz*factor;
        }else if(grounded){
            const float drag=std::exp(-supportFriction(solid,material)*h);
            velocity[0]*=drag;velocity[2]*=drag;
            if(std::hypot(velocity[0],velocity[2])<settings.stopSpeed){velocity[0]=0;velocity[2]=0;}
        }
        velocity[1] = std::max(-settings.terminalSpeed, velocity[1] - settings.gravity * h);
        grounded = false;
        moveAxis(0, velocity[0] * h, solid);
        moveAxis(2, velocity[2] * h, solid);
        moveAxis(1, velocity[1] * h, solid);
    }
}
}
