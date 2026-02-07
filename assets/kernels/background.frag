/*
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
    float t = pc.u_time / 5.0;
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
    vec4 colorOffset = vec4(0.0, 0.1, 0.1, 0.0);
    
    vec4 cosineArg = timeTerm + positionScale + colorOffset;
    vec4 cosineResult = 0.3 * cos(cosineArg);
    
    vec4 innerExpression = p.y + cosineResult;
    vec4 absExpression = abs(innerExpression);
    
    // Final calculation: tanh(0.2 / abs(...))
    o = tanh(0.2 / absExpression);
    
    outColor = o ;//- vec4(0.5,0.5,0.5,0.0);
}
*/
#version 450
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float time;
    float _pad;
} pc;

// the book of shaders CH10
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453123); } 

float snoise2D(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash(i);
    float b = hash(i + vec2(1.0,0.0));
    float c = hash(i + vec2(0.0,1.0));
    float d = hash(i + vec2(1.0,1.0));

    vec2 u = f*f*(3.0-2.0*f);

    return mix(a,b,u.x) +
           (c-a)*u.y*(1.0-u.x) +
           (d-b)*u.x*u.y;
}

void main(){
    vec2 p = (gl_FragCoord.xy - pc.resolution*0.5) / pc.resolution.y;
    float st = abs(sin(pc.time));

    mat2 m1 = mat2(1.0,1.0,-1.0,1.0);
    mat2 m2 = mat2(1.0,-1.0,1.0,1.0);

    vec2 a = abs(m2*p);

    float d = 0.1/abs(max(a.y-0.03,abs(p.y)-0.1))
            + 0.1/max(a.x,abs(a.y-0.12)-0.08);

    float n = 0.0;
    for(int i=0;i<6;i++) {
        p = -m1*p + pc.time*0.4;
        n += snoise2D(p);
    }
    n = n * 0.9 + 4.0;

    vec4 o = tanh(vec4(1.0,2.0,3.0,1.0) * d * n * 0.018);

    fragColor = o;
}
