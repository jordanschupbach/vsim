#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 shadowPosition;
layout(location=0) out vec4 outColor;
layout(set=0,binding=256,std140) uniform Frame {
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 camera;
    vec4 sunDirection;
    vec4 sunColor;
} frame;
layout(set=0,binding=0) uniform texture2D shadowTexture;
layout(set=0,binding=128) uniform samplerShadow shadowSampler;
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 material;
} object;
const float PI = 3.14159265359;
float shadow(vec3 normal, vec3 light) {
    vec3 p = shadowPosition.xyz / shadowPosition.w;
    p.xy = p.xy * 0.5 + 0.5;
    p.y = 1.0 - p.y; // NVRHI's negative-height viewport maps +Y to the texture's top.
    if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0))) || any(greaterThan(p.xy, vec2(1))))
        return 1.0;
    float bias = max(0.0005 * (1.0 - dot(normal, light)), 0.00015);
    vec2 texel = 1.0 / vec2(textureSize(sampler2DShadow(shadowTexture, shadowSampler), 0));
    float visibility = 0.0;
    for (int y=-1; y<=1; ++y)
        for (int x=-1; x<=1; ++x)
            visibility += texture(sampler2DShadow(shadowTexture, shadowSampler), vec3(p.xy + vec2(x,y)*texel, p.z-bias));
    return visibility / 9.0;
}
void main() {
    vec3 n = normalize(worldNormal);
    vec3 v = normalize(frame.camera.xyz - worldPosition);
    vec3 l = normalize(frame.sunDirection.xyz);
    vec3 h = normalize(l + v);
    float nl = max(dot(n,l), 0.0), nv = max(dot(n,v), 0.001), nh = max(dot(n,h), 0.0);
    float metallic = object.material.x, roughness = object.material.y;
    vec3 albedo = object.color.rgb;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 f = f0 + (1.0-f0) * pow(1.0-max(dot(h,v),0.0),5.0);
    float a = roughness*roughness, a2 = a*a;
    float denominator = nh*nh*(a2-1.0)+1.0;
    float distribution = a2 / max(PI*denominator*denominator, 0.00001);
    float k = (roughness+1.0)*(roughness+1.0)/8.0;
    float geometry = nv/(nv*(1.0-k)+k) * nl/(nl*(1.0-k)+k);
    vec3 specular = distribution*geometry*f/max(4.0*nv*nl,0.001);
    vec3 diffuse = (1.0-f)*(1.0-metallic)*albedo/PI;
    vec3 ambient = mix(vec3(0.055,0.065,0.09),vec3(0.28,0.34,0.43),n.y*0.5+0.5);
    vec3 color = (diffuse+specular)*frame.sunColor.rgb*nl*shadow(n,l)
               + ambient*albedo*(1.0-metallic*0.65) + albedo*object.material.z;
    outColor = vec4(color,1.0);
}
