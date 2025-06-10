#version 450

layout(location = 0) in vec3 fragColor;  // Interpolated vertex color
layout(location = 0) out vec4 outColor;  // Final fragment color

void main() {
    // compute procedural pattern (striped effect)
    float stripePattern = mod(gl_FragCoord.x * 0.1, 1.0) > 0.5 ? 1.0 : 0.2;

    // Combine the stripe pattern with the vertex color
    //vec3 patternColor = fragColor * stripePattern;
    vec3 patternColor = mix(vec3(0.0, 0.0, 1.0), fragColor, gl_FragCoord.x / 800.0);

    // set fragment color
    outColor = vec4(patternColor, 1.0); // include alpha
}
