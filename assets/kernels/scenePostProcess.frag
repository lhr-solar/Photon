#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_tex0;

void main() {
    vec4 source = texture(u_tex0, vUV);
    vec3 color = source.rgb;

    color *= 0.98;
    color = pow(color, vec3(1.0));
    color = mix(vec3(dot(color, vec3(0.2126, 0.7152, 0.0722))), color, 0.99);
    color *= vec3(0.995, 1.0, 1.015);
    color = clamp((color - 0.5) * 1.06 + 0.5, 0.0, 1.0);

    float dist = distance(vUV, vec2(0.5));
    float vignette = smoothstep(0.78, 0.18, dist);
    color *= mix(0.97, 1.01, vignette);

    fragColor = vec4(color, source.a);
}
