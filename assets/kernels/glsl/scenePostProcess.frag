#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float u_time;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_tex0;

vec3 linearToSrgb(vec3 linearValue) {
    linearValue = max(linearValue, vec3(0.0));
    bvec3 cutoff = lessThanEqual(linearValue, vec3(0.0031308));
    vec3 lower = linearValue * 12.92;
    vec3 higher = 1.055 * pow(linearValue, vec3(1.0 / 2.4)) - 0.055;
    return mix(higher, lower, cutoff);
}

vec3 rrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 acesFilmicToneMap(vec3 linearSrgb) {
    const mat3 ACESInputMat = mat3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777)
    );

    const mat3 ACESOutputMat = mat3(
        vec3( 1.60475, -0.10208, -0.00327),
        vec3(-0.53108,  1.10813, -0.07276),
        vec3(-0.07367, -0.00605,  1.07602)
    );

    // Slight underexposure leaves room for the key light and material highlights.
    vec3 color = linearSrgb * (0.68 / 0.6);
    color = ACESInputMat * color;
    color = rrtAndOdtFit(color);
    color = ACESOutputMat * color;
    return clamp(color, vec3(0.0), vec3(1.0));
}

float interleavedGradientNoise(vec2 pixel, float frame) {
    vec3 p = vec3(pixel, frame);
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 cinematicGrade(vec3 color, vec2 uv) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // A restrained bleach-bypass-style grade: denser midtones, slightly muted
    // color, cool shadows and warmer highlights.
    color = (color - vec3(0.18)) * 1.14 + vec3(0.18);
    color = mix(vec3(luminance), color, 0.88);

    float shadowWeight = 1.0 - smoothstep(0.04, 0.42, luminance);
    float highlightWeight = smoothstep(0.48, 0.92, luminance);
    color *= mix(vec3(1.0), vec3(0.84, 0.92, 1.06), shadowWeight * 0.20);
    color *= mix(vec3(1.0), vec3(1.05, 0.98, 0.88), highlightWeight * 0.12);

    vec2 centeredUv = uv * 2.0 - 1.0;
    float vignette = 1.0 - smoothstep(0.30, 1.65, dot(centeredUv, centeredUv)) * 0.24;
    return max(color * vignette, vec3(0.0));
}

void main() {
    vec4 source = texture(u_tex0, vUV);

    // A small alpha-aware unsharp pass makes panel edges, track boundaries, and
    // normal-map detail read more clearly without haloing the transparent backdrop.
    vec2 texel = 1.0 / max(pc.resolution, vec2(1.0));
    vec4 north = texture(u_tex0, vUV + vec2(0.0, texel.y));
    vec4 south = texture(u_tex0, vUV - vec2(0.0, texel.y));
    vec4 east = texture(u_tex0, vUV + vec2(texel.x, 0.0));
    vec4 west = texture(u_tex0, vUV - vec2(texel.x, 0.0));
    float neighborAlpha = north.a + south.a + east.a + west.a;
    vec3 neighborhood = (north.rgb * north.a + south.rgb * south.a +
                         east.rgb * east.a + west.rgb * west.a) /
                        max(neighborAlpha, 0.001);
    vec3 sharpened = max(source.rgb + (source.rgb - neighborhood) * 0.12, vec3(0.0));
    source.rgb = mix(source.rgb, sharpened, smoothstep(0.15, 0.95, source.a));

    vec3 graded = cinematicGrade(acesFilmicToneMap(source.rgb), vUV);
    float luminance = dot(graded, vec3(0.2126, 0.7152, 0.0722));
    float frame = floor(pc.u_time * 24.0);
    float grain = interleavedGradientNoise(gl_FragCoord.xy, frame) - 0.5;
    graded *= 1.0 + grain * mix(0.016, 0.007, smoothstep(0.05, 0.65, luminance));

    vec3 displayColor = linearToSrgb(max(graded, vec3(0.0)));
    fragColor = vec4(displayColor, source.a);
}
