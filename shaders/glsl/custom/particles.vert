#version 450

layout(location = 0) in vec3 inPosition;  // Particle position from CPU
layout(location = 0) out vec2 fragUV;     // Pass UV to fragment shader

void main() {
    fragUV = inPosition.xy * 0.5 + 0.5;  // Normalize coordinates for UV mapping
    gl_Position = vec4(inPosition, 1.0); // Set position
    gl_PointSize = 1.0;                 // Increase size for visibility
}
