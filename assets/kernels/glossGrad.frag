#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    float mr = min(pc.resolution.x, pc.resolution.y);
    vec2 uv = (fragCoord * 2.0 - pc.resolution) / mr;

    float d = -pc.u_time * 0.5;
    float a = 0.0;
    for (float i = 0.0; i < 8.0; ++i) {
        a += cos(i - d - a * uv.x);
        d += sin(uv.y * i + a);
    }
    d += pc.u_time * 0.5;

    vec3 col = vec3(cos(uv * vec2(d, a)) * 0.6 + 0.4,
                    cos(a + d) * 0.5 + 0.5);
    col = cos(col * cos(vec3(d, a, 2.5)) * 0.5 + 0.5);

    outColor = vec4(col, 1.0);
}
