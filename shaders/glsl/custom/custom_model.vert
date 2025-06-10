#version 450

layout(location = 0) in vec3 inPosition;  // Vertex position
layout(location = 1) in vec3 inNormal;    // Vertex normal
layout(location = 2) in vec3 inColor;     // Vertex color

layout(location = 0) out vec3 fragColor;  // Pass color to fragment shader

layout(binding = 0) uniform UBO {
    mat4 projection;  // Projection matrix
    mat4 modelview;   // Model-view matrix
    vec4 lightPos;    // Light position
} ubo;

void main() {
    // Transform vertex position using model-view-projection matrix
    gl_Position = ubo.projection * ubo.modelview * vec4(inPosition, 1.0);

    // Pass vertex color to fragment shader
    fragColor = inColor;
}
