#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec3 r = vec3(pc.resolution, 1.0);
    vec2 coords = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 u = (coords - pc.resolution * 0.5) / pc.resolution.y - 0.3;

    float s = 0.002;
    vec3 p = vec3(0.0);
    vec4 o = vec4(0.0);

    for (float i = 0.0; i < 32.0 && s > 0.001; i++) {
        o += vec4(5.0, 2.0, 1.0, 0.0) / length(u - 0.1);

        p += vec3(u * s, s);
        s = 1.0 + p.y;

        float n = 0.01;
        while (n < 1.0) {
            float term = abs(dot(sin(p.z + pc.u_time + p / n), vec3(1.0))) * n * 0.1;
            s += term;
            n += n;
        }
    }

    o = tanh(o / 500.0);
    outColor = o + vec4(0.0, 0.0, 0.0, 1.0);
}
