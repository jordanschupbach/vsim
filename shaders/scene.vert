#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
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
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 shadowPosition;
void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = transpose(inverse(mat3(object.model))) * inNormal;
    shadowPosition = frame.lightViewProjection * world;
    gl_Position = frame.viewProjection * world;
}
