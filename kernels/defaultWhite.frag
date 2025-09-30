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
    vec2 r    = pc.resolution;

    // same coordinate normalization
    vec2 p = (frag * 2.0 - r) / r.y / 0.1;

    float minTerm = min(p.y, p.y / 0.3);

    vec4 cosTerm = cos(p.x + cos(p.x * 0.6 - pc.u_time)
                       + vec4(0.0, 0.3, 0.6, 1.0));

    float sinTerm = sin(p.x * 0.4 - pc.u_time);

    vec4 denom = abs(minTerm / cosTerm / sinTerm);

    // original “ink” pattern
    vec4 pattern = tanh(0.4 / denom);

    // white background blend:
    // mix(white, patternColor, alpha)  where alpha = pattern luminance
    // Here we use pattern itself as the colour and alpha.
    vec3 baseColor = vec3(1.0);                // pure white background
    vec3 inkColor  = pattern.rgb;              // colourful foreground
    vec3 finalColor = mix(baseColor, inkColor, pattern.rgb);

    outColor = vec4(finalColor, 1.0);
}

