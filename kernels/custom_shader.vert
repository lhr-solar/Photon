#version 450
void main() {
    const vec2 POS[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(POS[gl_VertexIndex], 0.0, 1.0);
}
