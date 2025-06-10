#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    float gridSize = 20.0;
    vec2 grid = fract(fragUV * gridSize);
    float dist = length(grid - 0.5);

    float particle = smoothstep(0.05, 0.02, dist);

    outColor = vec4(1.0, 0.0, 0.0, 1.0);  
}

