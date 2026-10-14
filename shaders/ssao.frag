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
layout(set=0,binding=128) uniform sampler nearestSampler;
layout(push_constant) uniform Settings { vec4 values; } settings;
vec3 position(vec2 coord, float depth) {
    vec2 local = (coord - frame.viewport.xy) / frame.viewport.zw;
    vec4 world = frame.inverseViewProjection * vec4(local.x * 2 - 1, 1 - local.y * 2, depth, 1);
    return world.xyz / world.w;
}
float depthAt(vec2 coord) { return texture(sampler2D(depthTexture, nearestSampler), coord).r; }
void main() {
    float depth = depthAt(uv);
    vec3 normal = texture(sampler2D(normalTexture, nearestSampler), uv).xyz;
    visibility = 1;
    if (depth >= 1 || dot(normal, normal) < 0.5) return;
    normal = normalize(normal);
    vec3 origin = position(uv, depth);
    vec3 helper = abs(normal.z) < 0.999 ? vec3(0,0,1) : vec3(0,1,0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    // A deterministic rotation per pixel removes repeated directional bands.
    float angle = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898,78.233))) * 43758.5453) * 6.2831853;
    float radius = settings.values.x, bias = settings.values.y;
    float occluded = 0;
    for (int i = 0; i < 32; ++i) {
        float z = fract((float(i) + 0.5) * 0.618033989);
        float ring = sqrt(max(0, 1 - z*z));
        float phi = float(i) * 2.39996323 + angle;
        vec3 direction = tangent * cos(phi) * ring + bitangent * sin(phi) * ring + normal * z;
        float distanceScale = (float(i) + 0.5) / 32;
        distanceScale = mix(0.1, 1, distanceScale * distanceScale);
        vec3 samplePosition = origin + normal * bias + direction * radius * distanceScale;
        vec4 clip = frame.viewProjection * vec4(samplePosition, 1);
        if (clip.w <= 0) continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 sampleUv = (ndc.xy * vec2(0.5,-0.5) + 0.5) * frame.viewport.zw + frame.viewport.xy;
        if (ndc.z <= 0 || ndc.z >= 1 || any(lessThan(sampleUv,frame.viewport.xy)) ||
            any(greaterThanEqual(sampleUv,frame.viewport.xy + frame.viewport.zw))) continue;
        // Reconstruct at the fetched texel center, rather than at the requested
        // subpixel coordinate, to avoid inventing geometry on steep surfaces.
        ivec2 size = textureSize(sampler2D(depthTexture,nearestSampler),0);
        vec2 texelUv = (floor(sampleUv * vec2(size)) + 0.5) / vec2(size);
        float sampleDepth = depthAt(texelUv);
        if (sampleDepth >= 1) continue;
        vec3 surface = position(texelUv, sampleDepth);
        float rangeWeight = 1 - smoothstep(radius * 0.5, radius, length(surface - origin));
        float sampledDistance = length(frame.camera.xyz - samplePosition);
        float surfaceDistance = length(frame.camera.xyz - surface);
        float surfaceHeight = dot(surface-origin,normal);
        occluded += surfaceHeight > bias * 0.5 && surfaceDistance + bias < sampledDistance ? rangeWeight : 0;
    }
    visibility = clamp(1 - settings.values.z * occluded / 32, 0, 1);
}
