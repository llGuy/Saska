#version 450

layout(location = 0) out vec2 out_uvs;

layout(push_constant) uniform Push_Constant
{
    vec2 scale;
    vec2 position;
} push_k;

vec2 positions[] = vec2[]( vec2(-1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0) );

void
main(void)
{
    out_uvs = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    vec2 ss_p = out_uvs * 2.0 - 1.0;
    ss_p *= push_k.scale;
    ss_p += push_k.position;

    gl_Position = vec4(ss_p, 0.0f, 1.0f);
    gl_Position.y *= -1.0;
}
