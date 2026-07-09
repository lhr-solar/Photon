#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / pc.resolution;

    float d = -(pc.u_time * 0.3);
    float a = 0.0;

    for (float i = 0.0; i < 9.0; ++i) {
        a += cos(d + i * uv.x - a);
        d += 0.5 * sin(a + i * uv.y);
    }

    d += pc.u_time * 0.3;

    float r = cos(uv.x * a) * 0.7 + 0.3;
    float g = cos(uv.y * d) * 0.5 + 0.2;
    float b = cos(a + d) * 0.3 + 0.5;

    vec3 col = vec3(r, g, b);
    col = cos(col * cos(vec3(d, a, 2.5)) * 0.5 + 0.5);

    outColor = vec4(col, 1.0);
}
