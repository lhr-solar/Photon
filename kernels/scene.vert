#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inColor;

layout (binding = 0) uniform UBO
{
        mat4 projection;
        mat4 model;
        vec4 lightPos;
} ubo;

layout (push_constant) uniform ModelPC {
        mat4 transform;
        vec4 effectColor;
        int effectType;
} pc;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec4 outColor;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;
layout (location = 5) out vec4 outEffectColor;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
        outColor = inColor;
        outUV = inUV;
        outEffectColor = pc.effectColor;
        mat4 modelMat = ubo.model * pc.transform;
        gl_Position = ubo.projection * modelMat * vec4(inPos.xyz, 1.0);

        vec4 pos = modelMat * vec4(inPos, 1.0);
        
        // Use inverse transpose for normal transformation to handle non-uniform scaling
        mat3 normalMatrix = transpose(inverse(mat3(modelMat)));
        outNormal = normalize(normalMatrix * inNormal);
        
        vec3 lPos = ubo.lightPos.xyz;
	outLightVec = lPos - pos.xyz;
	outViewVec = -pos.xyz;		
}
