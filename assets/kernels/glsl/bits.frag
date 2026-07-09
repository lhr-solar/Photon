#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 p = (round(frag) - pc.resolution * 0.5) / pc.resolution.y;
    vec2 v = vec2(0.0);
    vec4 o = vec4(0.0);

    for (float i = 1.0; i <= 20.0; i += 1.0) {
        v = ceil(p);
        o += vec4(fwidth(v), fwidth(v).y, fract(length(v) / i - pc.u_time * 0.2)) * (1.0 - o.a);
        p += p;
    }

    outColor = vec4(clamp(o.rgb, 0.0, 1.0), 1.0);
}
