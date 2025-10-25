#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main() {
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec3 FC = vec3(frag, 0.0);
    vec3 r = vec3(pc.resolution.x, pc.resolution.y, pc.resolution.x);
    float t = pc.u_time;
    vec4 o = vec4(0.0);
    
    // Normalize coordinates to [-1, 1] range, aspect corrected
    vec2 p = (FC.xy * 2.0 - r.xy) / r.y;
    
    // Apply transformation matrix and scaling
    mat2 transformMatrix = mat2(9.0, -8.0, -8.0, 9.0);
    vec2 transformedP = p * transformMatrix * 5.0 / (4.0 + dot(p, p));
    
    // Add fractional and temporal components
    vec4 FC4 = vec4(FC, FC.x); // Extend FC to match yxyx swizzle
    float fractionalTerm = fract(dot(FC4, sin(FC4.yxyx + t)));
    float temporalTerm = t / 0.1;
    
    // Update p with all transformations
    p = transformedP + vec2(fractionalTerm) + vec2(temporalTerm);
    
    // Calculate complex sine expression
    float sineTerm = sin(t + p.x + p.y);
    float expTerm = exp(sineTerm);
    
    // Calculate dot product of cosines and sines
    vec2 cosArg = p + p.x / 7.0;
    vec2 sinArg = p.yx * 0.61;
    float dotCosSin = dot(cos(cosArg), sin(sinArg));
    
    // Calculate final color modulation
    float cosTerm = cos(p.x * 0.1) + 1.0;
    vec4 colorModulation = cosTerm * vec4(0.0, 0.1, 0.2, 0.0);
    vec4 finalSine = sin(dotCosSin + colorModulation) + 1.0;
    
    // Final color calculation
    o = tanh(0.1 * expTerm / finalSine);
    
    outColor = o;
}
