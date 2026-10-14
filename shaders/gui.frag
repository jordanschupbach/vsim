#version 450
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform texture2D fontTexture;
layout(set=0,binding=128) uniform sampler fontSampler;
void main() {
    vec4 srgb = tint * texture(sampler2D(fontTexture, fontSampler), texcoord);
    vec3 linear = mix(srgb.rgb / 12.92, pow((srgb.rgb + 0.055) / 1.055, vec3(2.4)),
                      greaterThan(srgb.rgb, vec3(0.04045)));
    color = vec4(linear, srgb.a);
}
