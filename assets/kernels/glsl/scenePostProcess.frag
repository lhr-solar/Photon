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

    vec3 color = linearSrgb * (0.85 / 0.6);
    color = ACESInputMat * color;
    color = rrtAndOdtFit(color);
    color = ACESOutputMat * color;
    return clamp(color, vec3(0.0), vec3(1.0));
}

void main() {
    vec4 source = texture(u_tex0, vUV);
    vec3 displayColor = linearToSrgb(acesFilmicToneMap(source.rgb));
    fragColor = vec4(displayColor, source.a);
}
