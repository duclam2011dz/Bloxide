#version 330 core

layout (location = 0) in uint aPacked;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uChunkOrigin;

out vec3 vColor;
out float vAo;

void main() {
    uint x = aPacked & 15u;
    uint y = (aPacked >> 4u) & 255u;
    uint z = (aPacked >> 12u) & 15u;
    uint block = (aPacked >> 16u) & 7u;
    uint ao = (aPacked >> 22u) & 3u;
    vec3 colors[5] = vec3[5](vec3(0.0), vec3(0.12, 0.12, 0.14), vec3(0.30, 0.72, 0.22), vec3(0.48, 0.27, 0.12), vec3(0.48, 0.52, 0.57));
    vColor = colors[block];
    vAo = 1.0 - float(ao) * 0.18;
    gl_Position = uProjection * uView * vec4(vec3(x, y, z) + uChunkOrigin, 1.0);
}
