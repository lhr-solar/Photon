#version 450

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

const float PI = 3.14159265358979323846;

mat3 rotate3D(float a, vec3 axis) {
    axis = normalize(axis);
    float s = sin(a), c = cos(a), oc = 1.0 - c;
    return mat3(
        oc*axis.x*axis.x + c,        oc*axis.x*axis.y - axis.z*s, oc*axis.z*axis.x + axis.y*s,
        oc*axis.x*axis.y + axis.z*s, oc*axis.y*axis.y + c,        oc*axis.y*axis.z - axis.x*s,
        oc*axis.z*axis.x - axis.y*s, oc*axis.y*axis.z + axis.x*s, oc*axis.z*axis.z + c
    );
}

void main() {
    vec2 r2 = pc.resolution;
    float t  = pc.u_time;

    vec3 FC = vec3(gl_FragCoord.xy, r2.x);            // FC.rgb
    vec3 rxx_y = vec3(r2.x, r2.x, r2.y);              // r.xxy

    vec4 o = vec4(0.0);
    for (float i = 0.0, z = 0.0, d = 0.0; i++ < 1e2; ) {
        vec3 p = z * normalize(FC * 2.0 - rxx_y);
        p.z += t * PI / 10.0;
        for (d = 2.0; d < 1024.0; d += d) {
            p += 0.5 * sin(rotate3D(d * 9.0, vec3(r2.x, r2.y, r2.y)) * (p * d) + t * PI / 10.0) / d;
        }
        z += d = (0.005 + 0.4 * abs(0.7 + p.y));
        o += (cos(p.y / 0.05 - vec4(0.0, 1.0, 2.0, 3.0) * 0.4 - 8.0) + 1.7) / d;
    }
    o = tanh(o / 1e4);
    fragColor = o;
}
