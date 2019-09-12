#version 450

layout(location = 0) out vec2 out_uvs;

layout(push_constant) uniform Push_Constant
{
    vec2 positions[4];
} push_k;

vec2 uvs[] = vec2[]( vec2(0, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1) );

void
main(void)
{
    out_uvs = uvs[gl_VertexIndex];

    gl_Position = vec4(push_k.positions[gl_VertexIndex], 0.0, 1.0);
    gl_Position.y *= -1.0;
}
