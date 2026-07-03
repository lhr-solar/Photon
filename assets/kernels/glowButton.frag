#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

float roundedCapsuleDistance(vec2 p, vec2 halfSegment, float radius) {
    vec2 nearest = clamp(p, -halfSegment, halfSegment);
    return length(p - nearest) - radius;
}

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 p = (frag * 2.0 - pc.resolution) / pc.resolution.y;
    float t = pc.u_time;

    // The original golfed code uses b.x = 0.6 and reuses b.y as the SDF distance.
    // Leaving b.y at 0.0 makes the primitive a pill-shaped rounded button:
    // length(p - clamp(p, -vec2(0.6, 0.0), vec2(0.6, 0.0))) - 0.5.
    vec2 b = vec2(0.6, 0.0);
    float d = roundedCapsuleDistance(p, b, 0.5);

    // Brightness field:
    //   6e-3 / max(d + rippleGate, d*d)
    // where rippleGate opens thin animated bands around the SDF surface.
    float wave = cos(d - p.x * 0.1 - t * 2.0);
    float rippleGate = 0.2 / exp(abs(wave) / 0.1);
    float field = 6e-3 / max(d + rippleGate, d * d);

    // Phase-shifted cosine palette from the original vec4(6, 1, 2, 3).
    vec4 phase = p.x / (abs(d) + 0.4) + t * 2.0 + vec4(6.0, 1.0, 2.0, 3.0);
    vec4 colorWave = cos(phase) + 1.2;

    outColor = sqrt(tanh(field * colorWave));
}
