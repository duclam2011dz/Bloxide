#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aFrom;
layout(location=2) in uint aLayer;
layout(location=3) in uint aFace;
layout(location=4) in uint aAo;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uChunkOrigin;
uniform float uMorph;
out vec2 vUv;
out vec3 vLocalPosition;
out float vAo;
flat out uint vBaseLayer;
flat out uint vFace;
void main(){
    vec3 p=mix(aFrom,aPosition,uMorph);vLocalPosition=p;
    vBaseLayer=aLayer;vFace=aFace;vAo=1.0-float(aAo)*0.18;
    if(aFace==0u)vUv=vec2(p.z,-p.y);
    else if(aFace==1u)vUv=vec2(-p.z,-p.y);
    else if(aFace==2u)vUv=p.xz;
    else if(aFace==3u)vUv=vec2(p.x,-p.z);
    else if(aFace==4u)vUv=vec2(-p.x,-p.y);
    else vUv=vec2(p.x,-p.y);
    gl_Position=uProjection*uView*vec4(p+uChunkOrigin,1);
}
