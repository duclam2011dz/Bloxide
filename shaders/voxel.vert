#version 330 core
layout(location=0) in uint aPacked;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uChunkOrigin;
out vec2 vUv;
out vec3 vLocalPosition;
out float vAo;
flat out uint vBaseLayer;
flat out uint vFace;
void main(){
    vec3 local=vec3(aPacked&31u,(aPacked>>5u)&511u,(aPacked>>14u)&31u);
    vBaseLayer=(aPacked>>19u)&127u;
    vFace=(aPacked>>26u)&7u;
    uint ao=(aPacked>>29u)&3u;
    if(vFace==0u)vUv=vec2(local.z,-local.y);
    else if(vFace==1u)vUv=vec2(-local.z,-local.y);
    else if(vFace==2u)vUv=local.xz;
    else if(vFace==3u)vUv=vec2(local.x,-local.z);
    else if(vFace==4u)vUv=vec2(-local.x,-local.y);
    else vUv=vec2(local.x,-local.y);
    vLocalPosition=local;
    vAo=1.0-float(ao)*0.18;
    gl_Position=uProjection*uView*vec4(local+uChunkOrigin,1);
}
