#version 330 core

in vec3 vColor;
in float vAo;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor * vAo, 1.0);
}
