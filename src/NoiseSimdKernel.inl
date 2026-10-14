#include <cmath>
namespace bloxide {
namespace {
inline std::uint64_t batchMix(std::uint64_t x){x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
inline std::uint64_t batchHash(std::uint64_t seed,std::int64_t x,std::int64_t y,std::int64_t z){
    auto a=batchMix(std::uint64_t(x)+0x9e3779b97f4a7c15ULL),b=batchMix(std::uint64_t(y)+0xbf58476d1ce4e5b9ULL),c=batchMix(std::uint64_t(z)+0x94d049bb133111ebULL);
    return batchMix(seed^a^((b<<21)|(b>>43))^((c<<42)|(c>>22)));
}
constexpr double gs=0.7071067811865475244;
constexpr double grad2[8][2]{{1,0},{-1,0},{0,1},{0,-1},{gs,gs},{-gs,gs},{gs,-gs},{-gs,-gs}};
constexpr double grad3[12][3]{{gs,gs,0},{-gs,gs,0},{gs,-gs,0},{-gs,-gs,0},{gs,0,gs},{-gs,0,gs},{gs,0,-gs},{-gs,0,-gs},{0,gs,gs},{0,-gs,gs},{0,gs,-gs},{0,-gs,-gs}};
BatchVector contribution2(BatchVector x,BatchVector y,BatchVector gx,BatchVector gy){
    auto t=bsub(bsub(bset(0.5),bmul(x,x)),bmul(y,y)),mask=bgt(t,bset(0));
    auto square=bmul(t,t),value=bmul(bmul(square,square),badd(bmul(gx,x),bmul(gy,y)));return band(mask,value);
}
BatchVector contribution3(BatchVector x,BatchVector y,BatchVector z,BatchVector gx,BatchVector gy,BatchVector gz){
    auto t=bsub(bsub(bsub(bset(0.6),bmul(x,x)),bmul(y,y)),bmul(z,z)),mask=bgt(t,bset(0));
    auto square=bmul(t,t),value=bmul(bmul(square,square),badd(badd(bmul(gx,x),bmul(gy,y)),bmul(gz,z)));return band(mask,value);
}
void points2(const SimplexNoise2D& n,const double* x,const double* y,double* out){
    constexpr double F=0.36602540378443864676,G=0.21132486540518711775;
    auto vx=bload(x),vy=bload(y),skew=bmul(badd(vx,vy),bset(F));
    double xi[BatchLanes],yi[BatchLanes],offset[BatchLanes],firstX[BatchLanes],firstY[BatchLanes];
    bstore(xi,badd(vx,skew));bstore(yi,badd(vy,skew));
    std::int64_t i[BatchLanes],j[BatchLanes];int ix[BatchLanes],iy[BatchLanes];
    for(int k=0;k<BatchLanes;++k){i[k]=std::int64_t(std::floor(xi[k]));j[k]=std::int64_t(std::floor(yi[k]));xi[k]=double(i[k]);yi[k]=double(j[k]);offset[k]=double(i[k]+j[k])*G;}
    auto x0=bsub(vx,bsub(bload(xi),bload(offset))),y0=bsub(vy,bsub(bload(yi),bload(offset)));
    bstore(firstX,x0);bstore(firstY,y0);for(int k=0;k<BatchLanes;++k){ix[k]=firstX[k]>firstY[k]?1:0;iy[k]=1-ix[k];}
    BatchVector sum=bset(0);
    for(int corner=0;corner<3;++corner){
        double gx[BatchLanes],gy[BatchLanes],ox[BatchLanes],oy[BatchLanes];
        for(int k=0;k<BatchLanes;++k){
            int dx=corner==0?0:corner==1?ix[k]:1,dy=corner==0?0:corner==1?iy[k]:1;
            auto g=NoiseBatchAccess::permutation(n)[batchHash(NoiseBatchAccess::seed(n),i[k]+dx,j[k]+dy,0)&255]%8;
            gx[k]=grad2[g][0];gy[k]=grad2[g][1];ox[k]=double(dx);oy[k]=double(dy);
        }
        auto xx=corner==0?x0:badd(bsub(x0,bload(ox)),bset(corner*G)),yy=corner==0?y0:badd(bsub(y0,bload(oy)),bset(corner*G));
        auto value=contribution2(xx,yy,bload(gx),bload(gy));sum=corner==0?value:badd(sum,value);
    }bstore(out,bmin(bmax(bmul(sum,bset(99)),bset(-1)),bset(1)));
}
void points3(const SimplexNoise3D& n,const double* x,const double* y,const double* z,double* out){
    constexpr double F=1.0/3,G=1.0/6;
    auto vx=bload(x),vy=bload(y),vz=bload(z),skew=bmul(badd(badd(vx,vy),vz),bset(F));
    double xi[BatchLanes],yi[BatchLanes],zi[BatchLanes],offset[BatchLanes],xx[BatchLanes],yy[BatchLanes],zz[BatchLanes];
    bstore(xi,badd(vx,skew));bstore(yi,badd(vy,skew));bstore(zi,badd(vz,skew));
    std::int64_t i[BatchLanes],j[BatchLanes],k[BatchLanes];int first[BatchLanes][3]{},second[BatchLanes][3]{};
    for(int lane=0;lane<BatchLanes;++lane){i[lane]=std::int64_t(std::floor(xi[lane]));j[lane]=std::int64_t(std::floor(yi[lane]));k[lane]=std::int64_t(std::floor(zi[lane]));
        xi[lane]=double(i[lane]);yi[lane]=double(j[lane]);zi[lane]=double(k[lane]);offset[lane]=double(i[lane]+j[lane]+k[lane])*G;}
    auto x0=bsub(vx,bsub(bload(xi),bload(offset))),y0=bsub(vy,bsub(bload(yi),bload(offset))),z0=bsub(vz,bsub(bload(zi),bload(offset)));
    bstore(xx,x0);bstore(yy,y0);bstore(zz,z0);
    for(int lane=0;lane<BatchLanes;++lane){
        int a,b,c,d,e,f;double a0=xx[lane],b0=yy[lane],c0=zz[lane];
        if(a0>=b0){if(b0>=c0){a=1;b=0;c=0;d=1;e=1;f=0;}else if(a0>=c0){a=1;b=0;c=0;d=1;e=0;f=1;}else{a=0;b=0;c=1;d=1;e=0;f=1;}}
        else{if(b0<c0){a=0;b=0;c=1;d=0;e=1;f=1;}else if(a0<c0){a=0;b=1;c=0;d=0;e=1;f=1;}else{a=0;b=1;c=0;d=1;e=1;f=0;}}
        first[lane][0]=a;first[lane][1]=b;first[lane][2]=c;second[lane][0]=d;second[lane][1]=e;second[lane][2]=f;
    }
    BatchVector sum=bset(0);
    for(int corner=0;corner<4;++corner){
        double gx[BatchLanes],gy[BatchLanes],gz[BatchLanes],ox[BatchLanes],oy[BatchLanes],oz[BatchLanes];
        for(int lane=0;lane<BatchLanes;++lane){
            int dx=corner==0?0:corner==1?first[lane][0]:corner==2?second[lane][0]:1,dy=corner==0?0:corner==1?first[lane][1]:corner==2?second[lane][1]:1,dz=corner==0?0:corner==1?first[lane][2]:corner==2?second[lane][2]:1;
            auto g=NoiseBatchAccess::permutation(n)[batchHash(NoiseBatchAccess::seed(n),i[lane]+dx,j[lane]+dy,k[lane]+dz)&255]%12;
            gx[lane]=grad3[g][0];gy[lane]=grad3[g][1];gz[lane]=grad3[g][2];ox[lane]=dx;oy[lane]=dy;oz[lane]=dz;
        }
        auto xc=corner==0?x0:badd(bsub(x0,bload(ox)),bset(corner*G)),yc=corner==0?y0:badd(bsub(y0,bload(oy)),bset(corner*G)),zc=corner==0?z0:badd(bsub(z0,bload(oz)),bset(corner*G));
        auto value=contribution3(xc,yc,zc,bload(gx),bload(gy),bload(gz));sum=corner==0?value:badd(sum,value);
    }bstore(out,bmin(bmax(bmul(sum,bset(45)),bset(-1)),bset(1)));
}
}
void BatchGrid2D(const SimplexNoise2D& n,const NoiseGrid2D& g,double* output){
    for(std::size_t z=0;z<g.depth;++z){std::size_t x=0;
        for(;x+BatchLanes<=g.width;x+=BatchLanes){double xx[BatchLanes],yy[BatchLanes];for(int lane=0;lane<BatchLanes;++lane){xx[lane]=g.x+double(x+lane)*g.dx;yy[lane]=g.z+double(z)*g.dz;}points2(n,xx,yy,output+z*g.width+x);}
        for(;x<g.width;++x)output[z*g.width+x]=n.sample(g.x+double(x)*g.dx,g.z+double(z)*g.dz);
    }
}
void BatchGrid3D(const SimplexNoise3D& n,const NoiseGrid3D& g,double* output){
    for(std::size_t y=0;y<g.height;++y)for(std::size_t z=0;z<g.depth;++z){std::size_t x=0;
        for(;x+BatchLanes<=g.width;x+=BatchLanes){double xx[BatchLanes],yy[BatchLanes],zz[BatchLanes];for(int lane=0;lane<BatchLanes;++lane){xx[lane]=g.x+double(x+lane)*g.dx;yy[lane]=g.y+double(y)*g.dy;zz[lane]=g.z+double(z)*g.dz;}points3(n,xx,yy,zz,output+(y*g.depth+z)*g.width+x);}
        for(;x<g.width;++x)output[(y*g.depth+z)*g.width+x]=n.sample(g.x+double(x)*g.dx,g.y+double(y)*g.dy,g.z+double(z)*g.dz);
    }
}
void BatchPoints3D(const SimplexNoise3D& n,const double*x,const double*y,const double*z,std::size_t count,double*out){
    std::size_t i=0;for(;i+BatchLanes<=count;i+=BatchLanes)points3(n,x+i,y+i,z+i,out+i);
    for(;i<count;++i)out[i]=n.sample(x[i],y[i],z[i]);
}
}
