#version 330 core
layout(location=0) in vec4 aPosition;
layout(location=1) in uint aLayer;
layout(location=2) in uint aFace;
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
    vec3 p=vec3(aPosition.x,mix(aPosition.w,aPosition.y,uMorph),aPosition.z);
    vLocalPosition=p;vBaseLayer=aLayer;vFace=aFace;vAo=1.0;
    vUv=aFace==2u?p.xz:((aFace<2u)?vec2(p.z,-p.y):vec2(p.x,-p.y));
    gl_Position=uProjection*uView*vec4(p+uChunkOrigin,1);
}
