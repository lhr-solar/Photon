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
    fragColor = texture(u_tex0, vUV);
}
