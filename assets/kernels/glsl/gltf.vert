#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 vertColor;
layout(location = 1) out vec2 vertUV;
layout(location = 2) out vec3 vertNormal;
layout(location = 3) out vec3 vertWorldPos;

layout(set = 0, binding = 0) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 camPos;
} ubo;

void main(){
    vec4 worldPos = ubo.model * vec4(inPos, 1.0);
    mat3 normalMat = mat3(transpose(inverse(ubo.model)));
    vertNormal = normalize(normalMat * inNormal);
    vertColor = inColor;
    vertUV = inUV;
    vertWorldPos = worldPos.xyz;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
