#version 450

layout(location = 0) out vec2 out_uvs;

layout(push_constant) uniform Push_Constant
{
    vec2 scale;
    vec2 position;
} push_k;

vec2 uvs[] = vec2[]( vec2(0, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1) );

void
main(void)
{
    out_uvs = uvs[gl_VertexIndex];

    vec2 ss_p = out_uvs * 2.0 - 1.0;
    ss_p *= push_k.scale;
    ss_p += push_k.position;

    gl_Position = vec4(ss_p, 0.0f, 1.0f);
}
