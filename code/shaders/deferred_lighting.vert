#version 450

layout(location = 0) out vec2 out_uvs;

void
main(void)
{
    out_uvs = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(out_uvs * 2.0f - 1.0f, 0.0f, 1.0f);
}
