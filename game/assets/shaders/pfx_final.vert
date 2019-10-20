#version 450

layout(location = 0) out VS_DATA
{
    vec2 uvs;
} vs_out;

vec2 uvs[] = vec2[]( vec2(0, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1) );

void
main(void)
{
    vs_out.uvs = uvs[gl_VertexIndex];
    gl_Position = vec4(vs_out.uvs * 2.0f - 1.0f, 0.0f, 1.0f);
}

