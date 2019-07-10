#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uvs;

layout(location = 0) out VS_DATA
{
    vec2 uvs;
} vs_out;

void
main(void)
{
    vs_out.uvs = uvs;
    gl_Position = vec4(position, 0.0, 1.0);
    gl_Position.y *= -1.0;
}
