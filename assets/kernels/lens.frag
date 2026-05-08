#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 r = pc.resolution;
    float t = pc.u_time;

    vec2 p = (frag * 2.0 - r) / r.y;
    float l = 0.8 - length(p);

    float waveScale = max(l / 0.1, 1.0 + l / 0.3);
    vec4 phase = (p.x + p.y) * waveScale + t + vec4(0.0, 1.0, 2.0, 0.0);

    float lensTerm = 0.1 / max(l, -l / 0.2);
    float focusTerm = 0.1 / length(p * l - 0.1);

    vec4 o = tanh(
        (3.0 + sin(phase)) * 0.2 * (lensTerm + focusTerm)
    );

    o *= o;
    outColor = vec4(clamp(o.rgb, 0.0, 1.0), 1.0);
}
