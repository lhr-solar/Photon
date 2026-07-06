#version 450
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float time;
    float _pad;
} pc;

// the book of shaders CH10
//float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453123); } 
float hash(vec2 p) {
    uvec2 n = uvec2(ivec2(floor(p)));
    n = (n ^ (n.yx >> 1U)) * 1664525U + 1013904223U;
    uint h = n.x ^ n.y ^ (n.x * 374761393U) ^ (n.y * 668265263U);
    return float(h & 0x00ffffffu) * (1.0 / 16777216.0);
}

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
    mat2 m3 = mat2(1.0,1.0,1.0,0.0);

    vec2 a = abs(m2*p);
    vec2 b = abs(m1*p);
    vec2 c = abs(m3*p);


    float d = 0.1/abs(max(a.y,abs(p.y)));
            //+ 0.1/max(a.x,abs(a.y-0.12)-0.08);

    float n = 0.0;
    for(int i=0;i<7;i++) {
        p = -m1*p + pc.time*0.4;
        n += snoise2D(p);
    }
    n = n * 0.9 + 4.0;

    vec4 o = tanh(vec4(3.0,3.0,3.0,1.0) * d * n * 0.010);

    vec2 noiseUV = gl_FragCoord.xy + vec2(pc.time * 23.0, pc.time * -11.0);
    vec3 noise = vec3(
        hash(noiseUV),
        hash(noiseUV.yx + 19.0),
        hash(noiseUV + 37.0)
    ) - 0.5;
    float grad = max(length(fwidth(o.rgb)), 1.0 / 48.0);
    o.rgb = clamp(o.rgb + noise * grad, 0.0, 1.0);

    fragColor = vec4(o.rgb, 1.0);
}
