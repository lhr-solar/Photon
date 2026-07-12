#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec3 FC = vec3(frag, 0.0);
    vec3 r = vec3(pc.resolution.x, pc.resolution.y, pc.resolution.x);
    float t = pc.u_time;

    vec4 o = vec4(0.0);
    float z = 0.0;
    float d = 0.0;

    for (float i = 1.0; i <= 100.0; i += 1.0) {
        vec3 p = z * normalize(FC.rgb * 2.0 - r.xyy);
        vec3 a = normalize(cos(vec3(4.0, 2.0, 0.0) + t - d / 0.1));

        p.z += 8.0;
        a = a * dot(a, p) - cross(a, p);

        d = 1.0;
        for (float j = 2.0; j <= 5.0; j += 1.0) {
            a += sin(a * j + t).yzx / j;
            d = j;
        }

        d = abs(length(a) - 5.0) / 6.0;
        z += d;
        o += vec4(3.0, 8.0, z, 0.0) / d / 90000.0;
    }

    outColor = vec4(clamp(o.rgb, 0.0, 1.0), 1.0);
}
