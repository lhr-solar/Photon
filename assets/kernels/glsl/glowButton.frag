#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

float roundedBoxDistance(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + vec2(radius);
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 local = frag - pc.resolution * 0.5;
    vec2 p = local / max(pc.resolution.y, 1.0);
    float t = pc.u_time;

    // Keep this in sync with GUI::drawButtonShaderOverlay().
    // The texture is larger than the real button by this padding on each side.
    const vec2 overlayPadding = vec2(28.0, 22.0);
    const float buttonRadius = 8.0;

    vec2 buttonHalfSize = max(pc.resolution * 0.5 - overlayPadding, vec2(buttonRadius));
    float dPixels = roundedBoxDistance(local, buttonHalfSize, buttonRadius);
    float d = dPixels / max(pc.resolution.y, 1.0);

    // Brightness field:
    //   6e-3 / max(d + rippleGate, d*d)
    // where rippleGate opens thin animated bands around the rounded button surface.
    float wave = cos(d - p.x * 0.1 - t * 2.0);
    float rippleGate = 0.2 / exp(abs(wave) / 0.1);
    float field = 6e-3 / max(d + rippleGate, d * d);

    // Phase-shifted cosine palette from the original vec4(6, 1, 2, 3).
    vec4 phase = p.x / (abs(d) + 0.4) + t * 2.0 + vec4(6.0, 1.0, 2.0, 3.0);
    vec4 colorWave = cos(phase) + 1.2;

    vec4 glow = sqrt(tanh(field * colorWave));
    float outside = max(dPixels, 0.0);
    float edgeFade = 1.0 - smoothstep(min(overlayPadding.x, overlayPadding.y) * 0.35,
                                      max(overlayPadding.x, overlayPadding.y), outside);
    float alpha = clamp(max(max(glow.r, glow.g), glow.b) * edgeFade * 0.95, 0.0, 1.0);
    outColor = vec4(glow.rgb, alpha);
}
