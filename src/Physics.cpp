#include <bloxide/Physics.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace bloxide {
namespace {
constexpr double Epsilon=1e-9;
struct Box{std::array<double,3> lo,hi;};
Box box(const std::array<double,3>& feet){return {{feet[0]-PlayerPhysics::Radius,feet[1],feet[2]-PlayerPhysics::Radius},{feet[0]+PlayerPhysics::Radius,feet[1]+PlayerPhysics::Height,feet[2]+PlayerPhysics::Radius}};}
template<class Fn> void voxels(const Box& area,Fn fn) {
    for(int y=int(std::floor(area.lo[1]));y<=int(std::floor(area.hi[1]));++y)
        for(int z=int(std::floor(area.lo[2]));z<=int(std::floor(area.hi[2]));++z)
            for(int x=int(std::floor(area.lo[0]));x<=int(std::floor(area.hi[0]));++x)fn(x,y,z);
}
bool overlap(const Box& a,int x,int y,int z){
    const int b[3]{x,y,z};
    for(int k=0;k<3;++k)if(a.hi[k]<=b[k]+Epsilon||a.lo[k]>=b[k]+1-Epsilon)return false;
    return true;
}
}
float PhysicsSettings::friction(BlockType b)const{
    switch(b){case BlockType::Grass:return grassFriction;case BlockType::Dirt:return dirtFriction;case BlockType::Bedrock:return bedrockFriction;default:return stoneFriction;}
}
void PlayerPhysics::rebase(){
    WorldPosition p{origin,feet[0],feet[1],feet[2]};p.normalize();origin=p.chunk;feet={p.x,p.y,p.z};
}
float PlayerPhysics::supportFriction(const SolidQuery& solid,const MaterialQuery& material)const {
    float coefficient=0;int y=int(std::floor(feet[1]-0.001));
    for(int x=int(std::floor(feet[0]-Radius+Epsilon));x<=int(std::floor(feet[0]+Radius-Epsilon));++x)
        for(int z=int(std::floor(feet[2]-Radius+Epsilon));z<=int(std::floor(feet[2]+Radius-Epsilon));++z){
            auto wx=origin.x*16+x,wz=origin.z*16+z;
            if(solid(wx,y,wz))coefficient=std::max(coefficient,settings.friction(material?material(wx,y,wz):BlockType::Stone));
        }
    return coefficient;
}
bool PlayerPhysics::overlapsBlock(std::int64_t x,int y,std::int64_t z)const {
    if(x<-WorldLimit-32||x>WorldLimit+32||z<-WorldLimit-32||z>WorldLimit+32)return false;
    auto a=box(feet);
    double lx=double(x-origin.x*16),lz=double(z-origin.z*16);
    return a.hi[0]>lx+Epsilon&&a.lo[0]<lx+1-Epsilon&&a.hi[1]>y+Epsilon&&a.lo[1]<y+1-Epsilon&&a.hi[2]>lz+Epsilon&&a.lo[2]<lz+1-Epsilon;
}
bool PlayerPhysics::clear(const SolidQuery& solid)const {
    bool free=true;auto a=box(feet);
    voxels(a,[&](int x,int y,int z){if(overlap(a,x,y,z)&&solid(origin.x*16+x,y,origin.z*16+z))free=false;});
    return free;
}
bool PlayerPhysics::recoverOverlap(const SolidQuery& solid) {
    if(clear(solid))return true;
    const auto original=feet;
    double best=std::numeric_limits<double>::infinity();
    std::array<double,3> chosen=feet;int chosenAxis=-1;
    // Fixed ties prefer horizontal recovery; no special upward teleport.
    for(auto direction:std::array<std::array<int,2>,6>{{{0,-1},{0,1},{2,-1},{2,1},{1,-1},{1,1}}}){
        int axis=direction[0],sign=direction[1];feet=original;
        for(int attempt=0;attempt<8;++attempt){
            auto a=box(feet);double adjustment=0;
            voxels(a,[&](int x,int y,int z){
                if(!overlap(a,x,y,z)||!solid(origin.x*16+x,y,origin.z*16+z))return;
                int cell[3]{x,y,z};
                if(sign>0)adjustment=std::max(adjustment,cell[axis]+1-a.lo[axis]+Epsilon*4);
                else adjustment=std::min(adjustment,cell[axis]-a.hi[axis]-Epsilon*4);
            });
            feet[axis]+=adjustment;
            double distance=std::abs(feet[axis]-original[axis]);
            if(distance>4||distance>=best)break;
            if(clear(solid)){best=distance;chosen=feet;chosenAxis=axis;break;}
        }
    }
    feet=chosen;
    if(chosenAxis<0){feet=original;velocity={};return false;}
    velocity[chosenAxis]=0;grounded=false;return true;
}
void PlayerPhysics::moveAxis(int axis,double delta,const SolidQuery& solid){
    if(delta==0)return;
    const auto current=box(feet);
    auto sweep=current;sweep.lo[axis]=std::min(current.lo[axis],current.lo[axis]+delta);sweep.hi[axis]=std::max(current.hi[axis],current.hi[axis]+delta);
    double allowed=delta;
    voxels(sweep,[&](int x,int y,int z){
        if(!solid(origin.x*16+x,y,origin.z*16+z))return;
        int cell[3]{x,y,z};
        for(int a=0;a<3;++a)if(a!=axis&&(current.hi[a]<=cell[a]+Epsilon||current.lo[a]>=cell[a]+1-Epsilon))return;
        if(delta>0){
            double face=cell[axis];
            if(face>=current.hi[axis]-Epsilon&&face<=current.hi[axis]+delta+Epsilon)
                allowed=std::min(allowed,std::max(0.0,face-current.hi[axis]));
        }else{
            double face=cell[axis]+1.0;
            if(face<=current.lo[axis]+Epsilon&&face>=current.lo[axis]+delta-Epsilon)
                allowed=std::max(allowed,std::min(0.0,face-current.lo[axis]));
        }
    });
    feet[axis]+=allowed;
    if(allowed!=delta){velocity[axis]=0;if(axis==1&&delta<0)grounded=true;}
}
void PlayerPhysics::step(float dt,float moveX,float moveZ,bool jump,const SolidQuery& solid,const MaterialQuery& material){
    if(dt<=0||!std::isfinite(dt))return;
    rebase();if(!recoverOverlap(solid))return;
    if(!std::isfinite(moveX)||!std::isfinite(moveZ)){moveX=moveZ=0;}
    double len=std::hypot(moveX,moveZ),wishX=moveX/std::max(1.0,len)*settings.walkSpeed,wishZ=moveZ/std::max(1.0,len)*settings.walkSpeed;
    if(jump&&grounded){velocity[1]=settings.jumpSpeed;grounded=false;}
    int count=std::max(1,int(std::ceil(dt/settings.maxSubstep)));double h=double(dt)/count;
    for(int i=0;i<count;++i){
        if(!grounded){double drag=std::exp(-settings.airDrag*h);velocity[0]*=drag;velocity[2]*=drag;}
        if(len>0){
            double dx=wishX-velocity[0],dz=wishZ-velocity[2],error=std::hypot(dx,dz);
            double factor=error>0?std::min(1.0,(grounded?settings.groundAcceleration:settings.airAcceleration)*h/error):0;
            velocity[0]+=dx*factor;velocity[2]+=dz*factor;
        }else if(grounded){
            double drag=std::exp(-supportFriction(solid,material)*h);velocity[0]*=drag;velocity[2]*=drag;
            if(std::hypot(velocity[0],velocity[2])<settings.stopSpeed)velocity[0]=velocity[2]=0;
        }
        velocity[1]=std::max(-double(settings.terminalSpeed),velocity[1]-settings.gravity*h);
        grounded=false;
        moveAxis(0,velocity[0]*h,solid);moveAxis(2,velocity[2]*h,solid);moveAxis(1,velocity[1]*h,solid);
    }
    rebase();
}
}
