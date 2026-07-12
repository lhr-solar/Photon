#version 450

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 FC = vec2(gl_FragCoord.x, pc.resolution - gl_FragCoord.y);
    vec2 r = pc.resolution;
    float t = pc.u_time;

    vec2 p = (FC * 2.0 - r) / r.y;
    float l = 2.0 - length(p - 1.0);
    float m = mod(dot(vec4(FC.xyxy), sin(vec4(FC.yyyx))) + t, 2.0) + sin(t + sin(t / 0.6 + p.y));
    vec4 base = vec4(1.0, 0.4, 0.2, 0.0);
    vec4 o = tanh(base / max(l, -l * 1e1) / exp(m)) + vec4(0.0, 0.0, 0.0, 1.0);
    fragColor = o;
}
