#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main()
{
    // pixel coords (flip Y so origin is top-left)
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);

    // `r` is a 2-lane resolution vector
    vec2 r = pc.resolution;

    // normalized coordinate system used by the effect
    // p = ((frag*2 - r) / r.y) / 0.1
    // -> center origin, aspect-correct, and zoom by 10x
    vec2 p = (frag * 2.0 - r) / r.y / 0.1;

    // oscillating denominator for the tanh field
    // we compute minTerm, cosTerm, sinTerm separately
    float minTerm = min(p.y, p.y / 0.3);

    // cosTerm = cos(p.x + cos(p.x*0.6 - t) + vec4(0,.3,.6,1))
    // the original used a vec4 to create 4 slightly shifted
    // color channels; we’ll compute it as a vec4 directly.
    vec4 cosTerm = cos(p.x + cos(p.x * 0.6 - pc.u_time) +
                       vec4(0.0, 0.3, 0.6, 1.0));

    // sinTerm = sin(p.x*0.4 - t)
    float sinTerm = sin(p.x * 0.4 - pc.u_time);

    // denominator for each channel
    vec4 denom = abs(minTerm / cosTerm / sinTerm);

    // Final color
    vec4 o = tanh(0.4 / denom);

    outColor = o;
}
