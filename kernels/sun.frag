#version 450

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec3 FC = vec3(gl_FragCoord.xy, 0.0);
    vec2 r = pc.resolution;
    float t = pc.u_time;

    vec4 o = vec4(0.0);

    for (float i = 0.0, z = 0.0, d = 0.0; i++ < 60.0; o += vec4(z, 3.0, 1.0, 1.0) / d) {
        vec3 p = z * normalize(FC * 2.0 - r.xyy);
        vec3 a = p;
        p.z += 8.0;

        for (d = 1.0; d++ < 9.0; a += sin(a * d + t + i).yzx / d) {}

        z += d = max(d = length(p) - 5.0, -d / 3.0) * 0.5 + length(sin(a / 0.3 + z) + 1.0) / 40.0;
    }

    o = tanh(o / 2000.0) + vec4(0.0,0.0,0.0,1.0);
    fragColor = o;
}
