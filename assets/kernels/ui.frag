#version 450
layout(binding=0) uniform sampler2D fontSampler;

layout(location=0) in vec2 inUV;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 fragPos;

layout(push_constant) uniform PushConstants {
    vec2 scale;
    vec2 translate;
    vec2 invScreenSize;
    vec2 whitePixel;
    vec4 gradTop;
    vec4 gradBottom;
    float u_time;
} pc;

layout(location=0) out vec4 outColor;

void main() {
    vec4 base = inColor * texture(fontSampler, inUV);

    float rawT = clamp(fragPos.y * pc.invScreenSize.y, 0.0, 1.0);

    float noise = (fract(sin(dot(fragPos.xy, vec2(12.9898,78.233))) * 43758.5453) - 0.5) * (1.0/255.0);
    rawT = clamp(rawT + noise, 0.0, 1.0);

    float t = smoothstep(0.0, 1.0, rawT);

    float breathe = 0.5 + 0.5 * sin(pc.u_time * 3.0); 
    t = clamp(t + 0.0 * (breathe - 0.5), 0.0, 1.0);

    vec3 grad = mix(pc.gradTop.rgb, pc.gradBottom.rgb, t);
    outColor = base;
}

