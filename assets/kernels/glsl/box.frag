#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float fsnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

mat3 rotate3D(float a, vec3 axis) {
    axis = normalize(axis);
    float s = sin(a);
    float c = cos(a);
    float oc = 1.0 - c;
    return mat3(
        oc * axis.x * axis.x + c,        oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
        oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c,        oc * axis.y * axis.z - axis.x * s,
        oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
    );
}

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec3 FC = vec3(frag, 0.0);
    vec3 r = vec3(pc.resolution, pc.resolution.y);
    float t = pc.u_time;

    vec3 p = vec3(0.0, 0.0, 3.0);
    vec3 v = vec3(FC.xy * 2.0 - r.xy, r.y);
    float T = 0.0;
    float d = 0.0;
    float n = fsnoise(ceil(FC.y / r + t / 0.3));
    vec4 o = vec4(0.0);

    for (float i = 1.0; i <= 200.0; i += 1.0) {
        mat3 rot = rotate3D(n * t, tan(r.yxy + n));
        d = min(length(max(abs(p * rot) + n - 1.0, vec3(-n * n * 0.8))) - n * n, 1.0 - p.y);
        T += d;

        if (i > 100.0) {
            d += 1e-5;
            v = fract(cos(t + sqrt(FC.x * FC.y) * vec3(9.0, 7.0, 5.0)) * 5e3);
            o.g = T;
        } else {
            T = 0.0;
        }

        p -= normalize(v) * d;
    }

    outColor = vec4(0.0, clamp(o.g, 0.0, 1.0), 0.0, 1.0);
}
