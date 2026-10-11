#include <bloxide/Targeting.hpp>
#include <cmath>
#include <limits>
namespace bloxide {
BlockTarget traceVoxels(const std::array<float,3>& origin,const std::array<float,3>& direction,
                       float reach,const std::function<TargetVoxel(std::int64_t,int,std::int64_t)>& query) {
    return traceWorldVoxels(WorldPosition::fromAbsolute(origin[0],origin[1],origin[2]),direction,reach,query);
}
BlockTarget traceWorldVoxels(const WorldPosition& world,const std::array<float,3>& direction,
                       float reach,const std::function<TargetVoxel(std::int64_t,int,std::int64_t)>& query) {
    WorldPosition checked=world;checked.normalize();const auto origin=checked.local();
    const auto baseX=checked.chunk.x*16,baseZ=checked.chunk.z*16;
    const double length=std::sqrt(double(direction[0])*direction[0]+double(direction[1])*direction[1]+double(direction[2])*direction[2]);
    if (!(length>0) || !std::isfinite(length) || reach<0) return {};
    int cell[3], step[3];
    double tMax[3], tDelta[3];
    for(int a=0;a<3;++a) {
        if(!std::isfinite(origin[a])) return {};
        cell[a]=static_cast<int>(std::floor(origin[a]));
        const double d=direction[a]/length;
        step[a]=(d>0)-(d<0);
        tDelta[a]=d==0 ? std::numeric_limits<double>::infinity() : std::abs(1/d);
        tMax[a]=d==0 ? tDelta[a] : ((step[a]>0 ? cell[a]+1.0 : double(cell[a]))-origin[a])/d;
    }
    double distance=0;
    Int3 normal{}, previous{cell[0],cell[1],cell[2]};
    for(int iterations=0; iterations<4096 && distance<=reach; ++iterations) {
        auto voxel=query(baseX+cell[0],cell[1],baseZ+cell[2]);
        if(voxel.blocked) return {};
        if(isSolid(voxel.type)) {
            // Starting inside a solid voxel has no visible entry face.
            if(normal==Int3{}) return {};
            return {true,{baseX+cell[0],cell[1],baseZ+cell[2]},previous,normal,distance,
                    voxel.type,voxel.instanceId,voxel.revision};
        }
        int axis=0;
        if(tMax[1]<tMax[axis]) axis=1;
        if(tMax[2]<tMax[axis]) axis=2;
        previous={baseX+cell[0],cell[1],baseZ+cell[2]};
        distance=tMax[axis];
        cell[axis]+=step[axis]; tMax[axis]+=tDelta[axis];
        normal={};
        if(axis==0) normal.x=-step[axis];
        if(axis==1) normal.y=-step[axis];
        if(axis==2) normal.z=-step[axis];
    }
    return {};
}
}
