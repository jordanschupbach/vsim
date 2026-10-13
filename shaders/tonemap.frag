#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform texture2D hdrTexture;
layout(set=0,binding=128) uniform sampler linearSampler;
layout(push_constant) uniform Settings { vec4 values; } settings;
void main() {
    vec4 sampleColor = texture(sampler2D(hdrTexture,linearSampler), uv);
    vec3 sky = mix(vec3(0.025,0.041,0.072), vec3(0.075,0.13,0.19), clamp(uv.y,0.0,1.0));
    vec3 c = sampleColor.a == 0.0 ? sky : sampleColor.rgb;
    c *= settings.values.x;
    c = clamp((c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14),0.0,1.0);
    if (settings.values.y > 0.5) c = pow(c,vec3(1.0/2.2));
    outColor = vec4(c,1.0);
}
