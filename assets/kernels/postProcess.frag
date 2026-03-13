#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_tex0;

const float TAU = 6.28318530718;
const int MC_SAMPLES = 8;
const float GRAIN_BLOCK_PX = 1.0;

uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float rand01(inout uint state) {
    state = hash_u32(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec2 rand2(inout uint state) {
    return vec2(rand01(state), rand01(state));
}

float interleavedGradientNoise(vec2 p) {
    vec2 f = fract(p * vec2(0.06711056, 0.00583715));
    return fract(52.9829189 * fract(f.x + f.y));
}

vec2 sampleDisk(vec2 u) {
    float a = TAU * u.x;
    float r = sqrt(u.y);
    return vec2(cos(a), sin(a)) * r;
}

void main() {
    vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
    vec2 pixel = 1.0 / pc.resolution;
    vec2 blockCoord = floor(gl_FragCoord.xy / GRAIN_BLOCK_PX);

    uvec2 ip = uvec2(blockCoord);
    uint frame = uint(pc.u_time * 30.0);
    uint baseState = ip.x * 73856093u ^ ip.y * 19349663u ^ frame * 83492791u ^ 0xA511E9B3u;

    vec3 accum = vec3(0.0);
    float wsum = 0.0;

    for (int i = 0; i < MC_SAMPLES; ++i) {
        uint s = baseState ^ (uint(i) * 0x9e3779b9u);
        vec2 r = rand2(s);
        vec2 disk = sampleDisk(r);

        float radiusPx = mix(0.8, 3.5, rand01(s));
        vec2 jitter = disk * radiusPx * pixel;
        vec3 c = texture(u_tex0, clamp(uv + jitter, vec2(0.0), vec2(1.0))).rgb;

        float w = 0.85 + 0.15 * rand01(s);
        accum += c * w;
        wsum += w;
    }

    vec3 color = accum / max(wsum, 1e-6);

    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float shadowBoost = 1.0 - smoothstep(0.08, 0.85, luma);
    float ign = interleavedGradientNoise(blockCoord + vec2(pc.u_time * 17.0, -pc.u_time * 9.0));

    uint grainState = baseState ^ 0xC2B2AE35u;
    float g = rand01(grainState) - 0.5;
    vec3 grain = vec3(g);
    vec3 mutedTint = vec3(0.96, 1.00, 0.97);
    grain *= mutedTint;

    float grainStrength = mix(0.045, 0.17, shadowBoost);
    grainStrength *= mix(0.85, 1.15, ign);

    color += grain * grainStrength;
    color = max(color, vec3(0.0));

    fragColor = vec4(color, 1.0);
}
