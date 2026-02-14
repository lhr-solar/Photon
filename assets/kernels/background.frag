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
    fragColor = vec4(o.rgb, 1.0);
}
