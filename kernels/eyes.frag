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
    
    // Calculate the complex expression step by step
    
    // First term: sin(p.y/0.4 + ...)
    float firstTerm = p.y / 0.4;
    
    // Fractal/noise term: fract(cos(dot(tan(p), r)) * 4e4)
    vec2 tanP = tan(p);
    float dotProduct = dot(tanP, r.xy);  // r is vec3, use xy components
    float largeCos = cos(dotProduct) * 40000.0;  // 4e4 = 40000
    float fractTerm = fract(largeCos);
    
    // Position terms: -p.x + p.y * cos(p/0.2 + cos(p/0.3)).x
    vec2 cosArg1 = p / 0.2 + cos(p / 0.3);
    float cosX = cos(cosArg1).x;  // Get x component of cos applied to vec2
    float positionTerm = -p.x + p.y * cosX;
    
    // Color offset
    vec4 colorOffset = vec4(0.0, 0.6, 1.0, 0.0);
    
    // Combine all terms in sine
    vec4 sineArg = firstTerm + fractTerm + positionTerm - colorOffset;
    vec4 sineResult = sin(sineArg) + 1.5;
    
    // Denominator: 2.5 + abs(cos(p.x/0.1))
    float denominator = 2.5 + abs(cos(p.x / 0.1));
    
    // Final result
    o = sineResult / denominator;
    
    outColor = o;
}
