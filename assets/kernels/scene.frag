#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inEffectColor;

layout (location = 0) out vec4 outFragColor;

layout (push_constant) uniform ModelPC {
    mat4 transform;
    vec4 effectColor;
    int effectType;
} pc;

void main() 
{
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = reflect(-L, N);
	float diffuse = max(dot(N, L), 0.0);
	vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.75);
    vec4 baseColor = vec4(diffuse * inColor * specular, 1.0);
    outFragColor = baseColor * inEffectColor;
    if(pc.effectType == 1){
        outFragColor.rgb = vec3(1.0) - outFragColor.rgb;
    } else if(pc.effectType == 2){
        float g = dot(outFragColor.rgb, vec3(0.299, 0.587, 0.114));
        outFragColor = vec4(g, g, g, outFragColor.a);
    }
}
