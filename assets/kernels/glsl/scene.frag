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

layout(set = 0, binding = 1) uniform sampler2D environmentIrradianceTex;
layout(set = 0, binding = 2) uniform sampler2D environmentSpecularTex;

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
    float _extensionPadding0;
    vec4 specularColorFactor;
    vec4 sheenColorFactor;
    float specularFactor;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float transmissionFactor;
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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
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

vec3 brdfLight(vec3 albedo, float metallic, float roughness, float diffuseWeight, vec3 F0, vec3 N, vec3 V, vec3 L, vec3 radiance) {
    vec3 H = normalize(V + L);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic) * diffuseWeight;
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

vec3 clearcoatLight(float clearcoat, float roughness, vec3 N, vec3 V, vec3 L, vec3 radiance) {
    vec3 H = normalize(V + L);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), vec3(0.04));
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return (D * G * F / max(4.0 * NdotV * NdotL, 1e-4)) * radiance * NdotL * clearcoat;
}

vec2 directionToEnvironmentUv(vec3 direction) {
    direction = normalize(direction);
    float u = atan(direction.y, direction.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(direction.z, -1.0, 1.0)) / PI;
    return vec2(u, v);
}

vec3 environmentIrradiance(vec3 N) {
    return texture(environmentIrradianceTex, directionToEnvironmentUv(N)).rgb;
}

vec3 environmentSpecular(vec3 R, float roughness) {
    float maxLod = float(textureQueryLevels(environmentSpecularTex) - 1);
    return textureLod(environmentSpecularTex, directionToEnvironmentUv(R), roughness * maxLod).rgb;
}

vec3 environmentBRDF(vec3 F0, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * ab.x + ab.y;
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

    float metallic = clamp(material.metallicFactor, 0.0, 1.0);
    float roughness = clamp(material.roughnessFactor, 0.04, 1.0);
    if (material.hasMetallicRoughnessTexture != 0) {
        vec4 mr = texture(metallicRoughnessTex, vertUV);
        roughness = clamp(material.roughnessFactor * mr.g, 0.04, 1.0);
        metallic = clamp(material.metallicFactor * mr.b, 0.0, 1.0);
    }

    vec3 albedo = base.rgb;
    float transmission = clamp(material.transmissionFactor, 0.0, 1.0);
    vec3 dielectricF0 = clamp(vec3(0.04) * material.specularColorFactor.rgb * material.specularFactor, vec3(0.0), vec3(1.0));
    vec3 F0 = mix(dielectricF0, albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic) * (1.0 - transmission * 0.65);

    vec3 keyL = normalize(vec3(5.0, 8.0, 6.0) - vertWorldPos);
    vec3 fillL = normalize(vec3(-6.0, 4.0, -5.0) - vertWorldPos);
    vec3 diffuseIbl = environmentIrradiance(N) * albedo / PI;
    vec3 specularIbl = environmentSpecular(reflect(-V, N), roughness) * environmentBRDF(F0, roughness, NdotV);
    vec3 ambient = kD * diffuseIbl + specularIbl * (1.0 - transmission * 0.35);
    float diffuseWeight = 1.0 - transmission * 0.65;
    vec3 directKey = brdfLight(albedo, metallic, roughness, diffuseWeight, F0, N, V, keyL, vec3(1.0) * 1.6);
    vec3 directFill = brdfLight(albedo, metallic, roughness, diffuseWeight, F0, N, V, fillL, vec3(0.2747, 0.4678, 1.0) * 0.45);
    float clearcoat = clamp(material.clearcoatFactor, 0.0, 1.0);
    float clearcoatRoughness = clamp(material.clearcoatRoughnessFactor, 0.03, 1.0);
    vec3 clearcoatSpec = clearcoatLight(clearcoat, clearcoatRoughness, N, V, keyL, vec3(1.0) * 1.6);
    clearcoatSpec += clearcoatLight(clearcoat, clearcoatRoughness, N, V, fillL, vec3(0.2747, 0.4678, 1.0) * 0.45);
    clearcoatSpec += environmentSpecular(reflect(-V, N), clearcoatRoughness) * environmentBRDF(vec3(0.04), clearcoatRoughness, NdotV) * clearcoat;
    float clearcoatEnergy = clearcoat * FresnelSchlickRoughness(NdotV, vec3(0.04), clearcoatRoughness).r;
    float sheenWeight = pow(1.0 - max(dot(N, V), 0.0), 4.0) * (1.0 - metallic);
    vec3 sheen = material.sheenColorFactor.rgb * sheenWeight * environmentIrradiance(N) / PI;

    if (material.hasOcclusionTexture != 0) {
        float ao = texture(occlusionTex, vertUV).r;
        ambient *= mix(1.0, ao, material.occlusionStrength);
    }

    vec3 emissive = material.emissiveFactor.rgb;
    if (material.hasEmissiveTexture != 0) {
        emissive *= texture(emissiveTex, vertUV).rgb;
    }

    vec3 baseLighting = ambient + directKey + directFill;
    vec3 color = baseLighting * (1.0 - clearcoatEnergy) + clearcoatSpec + sheen + emissive;
    fragColor = vec4(color, base.a);
}
