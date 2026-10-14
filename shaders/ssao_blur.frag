#version 450
layout(location=0) in vec2 uv;
layout(location=0) out float visibility;
layout(set=0,binding=256,std140) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 camera;
    vec4 sunDirection;
    vec4 sunColor;
    mat4 inverseViewProjection;
    vec4 viewport;
} frame;
layout(set=0,binding=0) uniform texture2D depthTexture;
layout(set=0,binding=1) uniform texture2D normalTexture;
layout(set=0,binding=2) uniform texture2D aoTexture;
layout(set=0,binding=128) uniform sampler nearestSampler;
layout(push_constant) uniform Settings { vec4 values; } settings;
vec3 position(vec2 coord, float depth) {
    vec2 local = (coord - frame.viewport.xy) / frame.viewport.zw;
    vec4 world = frame.inverseViewProjection * vec4(local.x*2-1, 1-local.y*2, depth, 1);
    return world.xyz / world.w;
}
void main() {
    float depth = texture(sampler2D(depthTexture,nearestSampler),uv).r;
    vec3 normal = texture(sampler2D(normalTexture,nearestSampler),uv).xyz;
    visibility = 1;
    if (depth >= 1 || dot(normal,normal) < 0.5) return;
    normal = normalize(normal);
    vec3 origin = position(uv,depth);
    vec2 pixel = 1 / vec2(textureSize(sampler2D(depthTexture,nearestSampler),0));
    vec2 axis = settings.values.w < 0.5 ? vec2(pixel.x,0) : vec2(0,pixel.y);
    float total = 0, weightSum = 0;
    for (int i = -3; i <= 3; ++i) {
        vec2 coord = uv + axis * float(i);
        if (any(lessThan(coord,frame.viewport.xy)) ||
            any(greaterThanEqual(coord,frame.viewport.xy + frame.viewport.zw))) continue;
        float neighborDepth = texture(sampler2D(depthTexture,nearestSampler),coord).r;
        if (neighborDepth >= 1) continue;
        vec3 neighborNormal = texture(sampler2D(normalTexture,nearestSampler),coord).xyz;
        float normalWeight = pow(max(0,dot(normal,neighborNormal)),16);
        vec3 neighbor = position(coord,neighborDepth);
        // Distance to the local tangent plane preserves contact and silhouette
        // discontinuities without weakening filtering on a sloped flat surface.
        float separation = abs(dot(neighbor-origin,normal));
        float weight = exp(-float(i*i)/8) * exp(-separation / (settings.values.x * 0.1)) * normalWeight;
        total += texture(sampler2D(aoTexture,nearestSampler),coord).r * weight;
        weightSum += weight;
    }
    visibility = weightSum > 0 ? total / weightSum : 1;
}
