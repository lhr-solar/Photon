#version 450
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2  resolution;
    float u_time;
    float _pad;
} pc;

void main(){
    vec2 n = vec2(gl_FragCoord) / vec2(pc.resolution);
    outColor = vec4(0, n.y, n.x, 1.0);
}
