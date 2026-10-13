#version 450
layout(location=0) in vec3 inPosition;
layout(set=0,binding=256,std140) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 camera;
    vec4 sunDirection;
    vec4 sunColor;
} frame;
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 material;
} object;
void main() { gl_Position = frame.lightViewProjection * object.model * vec4(inPosition, 1.0); }
