#version 330 core
in vec2 vUv;
in vec3 vWorldPosition;
in float vAo;
flat in uint vBaseLayer;
flat in uint vFace;
uniform sampler2DArray uBlockTextures;
uniform uint uTextureSeed;
out vec4 FragColor;
uint variant(ivec3 cell){
    uint h=uint(cell.x)*73856093u ^ uint(cell.y)*19349663u ^ uint(cell.z)*83492791u ^ vBaseLayer*2654435761u ^ uTextureSeed;
    h^=h>>16u;h*=0x7feb352du;h^=h>>15u;h*=0x846ca68bu;h^=h>>16u;
    return h&3u;
}
void main(){
    vec3 normals[6]=vec3[6](vec3(-1,0,0),vec3(1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,-1),vec3(0,0,1));
    ivec3 owner=ivec3(floor(vWorldPosition-normals[vFace]*0.001));
    uint layer=vBaseLayer+variant(owner);
    // Unwrapped block-space UV derivatives preserve mip selection across repeats.
    vec4 texel=textureGrad(uBlockTextures,vec3(vUv,float(layer)),dFdx(vUv),dFdy(vUv));
    FragColor=vec4(texel.rgb*vAo,1);
}
