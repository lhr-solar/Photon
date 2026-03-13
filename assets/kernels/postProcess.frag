/*
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
*/

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_tex0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float tri(float x) {
    return abs(fract(x) - 0.5);
}

void main() {
    vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
    vec2 pixel = 1.0 / pc.resolution;
    vec2 centered = uv * 2.0 - 1.0;
    centered.x *= pc.resolution.x / max(pc.resolution.y, 1.0);

    float radius = length(centered);
    vec2 warpUv = uv + centered * radius * 0.025;

    vec2 aberration = centered * (0.002 + radius * 0.006);
    float pulse = 0.5 + 0.5 * sin(pc.u_time * 0.7);
    aberration *= mix(0.8, 1.2, pulse);

    vec3 color;
    color.r = texture(u_tex0, clamp(warpUv + aberration, vec2(0.0), vec2(1.0))).r;
    color.g = texture(u_tex0, clamp(warpUv, vec2(0.0), vec2(1.0))).g;
    color.b = texture(u_tex0, clamp(warpUv - aberration, vec2(0.0), vec2(1.0))).b;

    vec3 bloom = vec3(0.0);
    float bloomWeight = 0.0;
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float angle = pc.u_time * 0.35 + fi * 1.04719755;
        vec2 dir = vec2(cos(angle), sin(angle));
        float dist = (2.0 + fi * 2.2);
        vec3 tap = texture(u_tex0, clamp(warpUv + dir * pixel * dist, vec2(0.0), vec2(1.0))).rgb;
        float w = smoothstep(0.45, 1.1, luminance(tap));
        bloom += tap * w;
        bloomWeight += w;
    }

    bloom /= max(bloomWeight, 1e-4);
    color += bloom * 0.28;

    float scan = sin(gl_FragCoord.y * 1.35 + pc.u_time * 5.5) * 0.03;
    float bands = tri(gl_FragCoord.y * 0.012 - pc.u_time * 0.2);
    color *= 1.0 + scan;
    color *= mix(0.96, 1.04, smoothstep(0.08, 0.45, bands));

    float hot = smoothstep(0.55, 1.25, luminance(color));
    color = mix(color, color * vec3(1.08, 1.02, 0.96), hot * 0.45);

    float vignette = smoothstep(1.15, 0.22, radius);
    color *= vignette;

    color = pow(max(color, vec3(0.0)), vec3(0.95));
    fragColor = vec4(color, 1.0);
}
