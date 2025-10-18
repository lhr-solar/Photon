#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main(){
    vec2 frag = vec2(gl_FragCoord.x, pc.resolution.y - gl_FragCoord.y);
    vec2 r = pc.resolution;
    vec2 p = (frag * 2.0 - r) / r.y / 0.1;
    float minTerm = min(p.y, p.y / 0.3);
    vec4 cosTerm = cos(p.x + cos(p.x * 0.6 - pc.u_time) + vec4(0.0, 0.3, 0.6, 1.0));
    float sinTerm = sin(p.x * 0.4 - pc.u_time);
    vec4 denom = abs(minTerm / cosTerm / sinTerm);
    vec4 o = tanh(0.4 / denom);
    outColor = o;
}
