#version 450

layout(location = 0) in vec3 vertColor;
layout(location = 1) in vec2 vertUV;
layout(location = 2) in vec3 vertNormal;
layout(location = 3) in vec3 vertWorldPos;

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform MVP {
    mat4 model;
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
    float angle = pc.u_time * 1.0;
    vec3 L = normalize(vec3(cos(angle), sin(angle), 0.8));
    /*
    float angleZ = pc.u_time * 0.7;
    float angleY = pc.u_time * 0.45;
    vec3 L0 = normalize(vec3(1.0, 0.0, 0.8));
    mat3 rotZ = mat3(
        cos(angleZ), -sin(angleZ), 0.0,
        sin(angleZ),  cos(angleZ), 0.0,
        0.0,          0.0,         1.0
    );
    mat3 rotY = mat3(
         cos(angleY), 0.0, sin(angleY),
         0.0,         1.0, 0.0,
        -sin(angleY), 0.0, cos(angleY)
    );
    vec3 L = normalize(rotY * (rotZ * L0));
    */
    vec3 H = normalize(V + L);

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
    float G = GeometrySmith(N, V, L, roughness);
    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = numerator / max(denominator, 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);

    vec3 radiance = vec3(2.5);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    vec3 ambient = vec3(0.02) * albedo;

    if (material.hasOcclusionTexture != 0) {
        float ao = texture(occlusionTex, vertUV).r;
        ambient *= mix(1.0, ao, material.occlusionStrength);
    }

    vec3 emissive = material.emissiveFactor.rgb;
    if (material.hasEmissiveTexture != 0) {
        emissive *= texture(emissiveTex, vertUV).rgb;
    }

    vec3 color = ambient + Lo + emissive;
    color = pow(color, vec3(1.0 / 2.2));


    fragColor = vec4(color, base.a);
}
