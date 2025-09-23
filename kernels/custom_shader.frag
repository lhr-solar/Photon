#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main()
{
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec3 FC = vec3(frag, 0.0);
    vec3 r  = vec3(pc.resolution.x, pc.resolution.y, pc.resolution.x);

    vec4 o = vec4(0.0);

    for (float z = 0.0, d = 0.0, i = 0.0; i++ < 20.0; ){
        // Sample point along the ray
        vec3 p = z * normalize(FC * 2.0 - r.xyx);

        // Polar & depth transformations
        p = vec3(
            atan(p.y / 0.2, p.x) * 2.0,
            p.z / 3.0,
            length(p.xy) - 5.0 - z * 0.2
        );

        // Turbulence/refraction loop
        for (d = 1.0; d < 7.0; d += 1.0)
            p += sin(p.yzx * d + pc.u_time + 0.3 * i) / d;

        // Distance estimate
        z += d = length(vec4(0.4 * cos(p) - 0.4, p.z));

        // Accumulate color and brightness
        o += (cos(p.x + i * 0.4 + z + vec4(6.0, 1.0, 2.0, 0.0)) + 1.0) / d;
    }
    o = tanh(o * o / 400.0);

    outColor = o;
}
