#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float u_time;
} pc;

void main() {
    vec2 centered = vUV - vec2(0.5);
    float dist = length(centered);
    float pulse = 0.5 + 0.5 * sin(pc.u_time * 1.5);
    float fade = smoothstep(pulse, pulse - 0.35, dist);

    vec3 baseA = vec3(0.18, 0.42, 0.92);
    vec3 baseB = vec3(0.90, 0.24, 0.48);
    vec3 color = mix(baseA, baseB, clamp(vUV.x, 0.0, 1.0));

    outColor = vec4(color * fade, fade);
}
