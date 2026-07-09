#version 450

layout(location = 0) in vec3 vertColor;
layout(location = 1) in vec2 vertUV;
layout(location = 2) in vec3 vertNormal;
layout(location = 3) in vec3 vertWorldPos;

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform ScenePushConstants {
    vec2 resolution;
    float u_time;
    float _pad;
    mat4 model;
} pc;

layout(set = 0, binding = 0) uniform SceneViewProjection {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} ubo;

layout(set = 1, binding = 0) uniform MaterialParams {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    int alphaMode;
    int hasBaseColorTexture;
    int hasMetallicRoughnessTexture;
    int hasNormalTexture;
    int hasOcclusionTexture;
    int hasEmissiveTexture;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTex;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;
layout(set = 1, binding = 4) uniform sampler2D occlusionTex;
layout(set = 1, binding = 5) uniform sampler2D emissiveTex;

const float PI = 3.14159265358979323846;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / max(denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / max(denom, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 toneAwareBoost(vec3 color, float amount) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float mask = smoothstep(0.08, 0.85, luma);
    return mix(color, color * amount, mask);
}

vec3 getNormalFromMap(vec3 N, vec2 uv) {
    vec3 tangentNormal = texture(normalTex, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    vec3 Q1 = dFdx(vertWorldPos);
    vec3 Q2 = dFdy(vertWorldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);
    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    vec4 texColor = material.hasBaseColorTexture != 0
        ? texture(baseColorTex, vertUV)
        : vec4(1.0);
    vec4 base = material.baseColorFactor * texColor * vec4(vertColor, 1.0);

    if (material.alphaMode == 1 && base.a < material.alphaCutoff) {
        discard;
    }

    vec3 N = normalize(vertNormal);
    if (material.hasNormalTexture != 0) {
        N = getNormalFromMap(N, vertUV);
    }

    vec3 V = normalize(ubo.camPos.xyz - vertWorldPos);

    float sunAngle = pc.u_time * 0.18 + 0.65;
    vec3 sunDir = normalize(vec3(cos(sunAngle) * 0.65, sin(sunAngle) * 0.35, 1.45));
    vec3 fillDir = normalize(vec3(-0.55, 0.25, 0.65));
    vec3 rimDir = normalize(vec3(-0.25, -0.85, 0.55));
    vec3 H = normalize(V + sunDir);

    float metallic = clamp(material.metallicFactor, 0.0, 1.0);
    float roughness = clamp(material.roughnessFactor, 0.04, 1.0);
    if (material.hasMetallicRoughnessTexture != 0) {
        vec4 mr = texture(metallicRoughnessTex, vertUV);
        roughness = clamp(material.roughnessFactor * mr.g, 0.04, 1.0);
        metallic = clamp(material.metallicFactor * mr.b, 0.0, 1.0);
    }

    vec3 albedo = base.rgb;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, sunDir, roughness);
    vec3 numerator = D * G * F;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, sunDir), 0.0);
    float denominator = 4.0 * NdotV * NdotL;
    vec3 specular = numerator / max(denominator, 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotFill = max(dot(N, fillDir), 0.0);
    float sky = 0.5 + 0.5 * N.z;
    float ground = 0.5 + 0.5 * (-N.z);
    float rim = pow(clamp(1.0 - NdotV, 0.0, 1.0), 2.2) * max(dot(N, rimDir), 0.0);

    vec3 sunRadiance = vec3(3.85, 4.15, 4.7);
    vec3 fillRadiance = vec3(0.72, 0.82, 0.96);
    vec3 skyAmbient = vec3(0.16, 0.19, 0.27) * sky;
    vec3 groundAmbient = vec3(0.045, 0.05, 0.06) * ground;
    vec3 ambient = (skyAmbient + groundAmbient + vec3(0.035)) * albedo;
    vec3 directSun = (kD * albedo / PI + specular) * sunRadiance * NdotL;
    vec3 directFill = (kD * albedo / PI) * fillRadiance * NdotFill * 0.5;
    vec3 rimLight = mix(vec3(0.0), vec3(0.16, 0.20, 0.26), rim) * (1.0 - roughness * 0.45);

    if (material.hasOcclusionTexture != 0) {
        float ao = texture(occlusionTex, vertUV).r;
        ambient *= mix(1.0, ao, material.occlusionStrength);
    }

    vec3 emissive = material.emissiveFactor.rgb;
    if (material.hasEmissiveTexture != 0) {
        emissive *= texture(emissiveTex, vertUV).rgb;
    }

    vec3 color = ambient + directSun + directFill + rimLight + emissive;
    color = toneAwareBoost(color, 1.03);
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, base.a);
}
