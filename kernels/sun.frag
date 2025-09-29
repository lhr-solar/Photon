#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;   // viewport size in pixels
    float u_time;       // time in seconds
    float _pad;
} pc;

void main()
{
    // ---------------------------------------------------------
    // 1) Pixel coordinates with top-left origin (Shadertoy-style)
    // ---------------------------------------------------------
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);

    // r holds resolution components used with swizzles
    vec3 r = vec3(pc.resolution.x, pc.resolution.y, pc.resolution.x);

    // final accumulated color
    vec4 o = vec4(0.0);

    // ---------------------------------------------------------
    // 2) Main march loop
    //    for(float i,z,d; i++<60.0; o += vec4(z,3,1,1)/d)
    // ---------------------------------------------------------
    float i = 0.0;
    float z = 0.0;
    float d = 0.0;

    for (; i++ < 60.0; )
    {
        // --- construct and initialize p and a ---
        vec3 p = z * normalize(frag.xyxx * vec4(1.0,1.0,0.0,0.0).xyz * 2.0 - r.xyy);
        // more simply: vec3 p = z * normalize(vec3(frag*2.0,0.0) - r.xyy);
        vec3 a = p;

        // shift along Z axis
        p.z += 8.0;

        // -----------------------------------------------------
        // inner turbulence/refraction loop
        // for(d=1.; d++<9.; a += sin(a*d + t + i).yzx / d)
        // -----------------------------------------------------
        for (d = 1.0; d++ < 9.0; )
        {
            a += sin(a * d + pc.u_time + i).yzx / d;
        }

        // -----------------------------------------------------
        // distance-field step
        // z += d = max(d = length(p) - 5., -d/3.) * 0.5
        //         + length(sin(a / 0.3 + z) + 1.) / 40.0
        // -----------------------------------------------------
        d = length(p) - 5.0;
        d = max(d, -d / 3.0) * 0.5
          + length(sin(a / 0.3 + z) + 1.0) / 40.0;
        z += d;

        // accumulate color (matches o += vec4(z,3,1,1)/d)
        o += vec4(z, 3.0, 1.0, 1.0) / d;
    }

    // ---------------------------------------------------------
    // 3) Final tone-mapping
    //    o = tanh(o / 2000.0)
    // ---------------------------------------------------------
    o = tanh(o / 2000.0);

    outColor = o;
}
