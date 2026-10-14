#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
layout(push_constant) uniform Screen { vec2 scale; vec2 translate; } screen;
void main() {
    gl_Position = vec4(position * screen.scale + screen.translate, 0, 1);
    texcoord = uv;
    tint = color;
}
