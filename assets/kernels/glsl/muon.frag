#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants{
    vec2 resolution;
    float u_time;
    float _pad;
} pc;

void main(){
    vec3 FC = vec3(gl_FragCoord.x, gl_FragCoord.y, 0.0);
    vec2 r = pc.resolution;
    float t = pc.u_time / 20.0;
    vec4 o = vec4(0.0, 0.0, 0.0, 1.0);
    float z = 0, d = 0, s = 0;

    for(float i = 0; i++ < 1e1;){
        vec3 p = vec3(0), a = vec3(0.0);
        p = z * normalize(FC.rgb*2.-r.xyy);
        a = normalize(cos(vec3(7,1,0)+t-s));
        p.z+=9;
        a=a*dot(a,p)-cross(a,p);
        for(d=1.; d++<9.;)
            a+=sin(a*d+t).yzx/d;
        z+=d=.03*abs(sin(s=length(a)));
        o+=(cos(z/.03+t+vec4(0,2,3,0))+1.)/d/s;
    }
    o = tanh(o/3e3);
    outColor = o;
}
