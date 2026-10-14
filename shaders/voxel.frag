#version 330 core
in vec2 vUv;
in vec3 vLocalPosition;
in float vAo;
flat in uint vBaseLayer;
flat in uint vFace;
uniform sampler2DArray uBlockTextures;
uniform uint uTextureSeed;
uniform uvec2 uChunkBlockX;
uniform uvec2 uChunkBlockZ;
uniform int uAlphaMode;
uniform float uTime;
uniform bool uUnderwater;
uniform vec3 uEyePosition;
uniform vec3 uChunkOrigin;
out vec4 FragColor;
uvec2 add64(uvec2 base,int delta){
    uint lo=base.x+uint(delta);
    uint hi=base.y+(delta<0?0xffffffffu:0u)+(lo<base.x?1u:0u);
    return uvec2(lo,hi);
}
uint variant(ivec3 cell){
    uvec2 x=add64(uChunkBlockX,cell.x),z=add64(uChunkBlockZ,cell.z);
    uint h=x.x*73856093u ^ x.y*0x52dce729u ^ uint(cell.y)*19349663u ^ z.x*83492791u ^ z.y*0x38495ab5u ^ vBaseLayer*2654435761u ^ uTextureSeed;
    h^=h>>16u;h*=0x7feb352du;h^=h>>15u;h*=0x846ca68bu;h^=h>>16u;
    return h&3u;
}
void main(){
    vec3 normals[6]=vec3[6](vec3(-1,0,0),vec3(1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,-1),vec3(0,0,1));
    ivec3 owner=ivec3(floor(vLocalPosition-normals[vFace]*0.001));
    uint layer=vBaseLayer+variant(owner);
    // Unwrapped block-space UV derivatives preserve mip selection across repeats.
    vec2 uv=vUv;if(vBaseLayer==32u)uv+=vec2(uTime*0.035,uTime*0.017);
    vec4 texel=textureGrad(uBlockTextures,vec3(uv,float(layer)),dFdx(vUv),dFdy(vUv));
    if(uAlphaMode==1&&texel.a<0.5)discard;
    vec3 color=texel.rgb*vAo;
    if(uUnderwater){float d=length(vLocalPosition+uChunkOrigin-uEyePosition);color=mix(color,vec3(0.04,0.23,0.36),1.0-exp(-d*0.055));}
    FragColor=vec4(color,uAlphaMode==2?texel.a:1.0);
}
