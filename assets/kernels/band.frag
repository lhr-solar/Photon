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
    
    // Normalize coordinates
    vec2 p = (FC.xy * 2.0 - r.xy) / r.y;
    
    // Calculate the dot product term for noise generation
    vec4 FC4 = vec4(FC, FC.x);  // Extend FC to vec4 for yxyx swizzle
    float dotTerm = dot(FC4, sin(FC4.yxyx));
    float noiseTerm = 0.1 * fract(dotTerm);
    
    // Calculate the main expression inside absolute value
    float timeTerm = t + noiseTerm;
    vec4 positionScale = p.x * vec4(0.7, 1.0, 1.3, 0.0);
    vec4 colorOffset = vec4(0.0, 1.0, 2.0, 0.0);
    
    vec4 cosineArg = timeTerm + positionScale + colorOffset;
    vec4 cosineResult = 0.3 * cos(cosineArg);
    
    vec4 innerExpression = p.y + cosineResult;
    vec4 absExpression = abs(innerExpression);
    
    // Final calculation: tanh(0.2 / abs(...))
    o = tanh(0.2 / absExpression);
    
    outColor = o;
}
