#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uvs;
layout(location = 2) in uint color;

layout(location = 0) out VS_DATA
{
    vec2 uvs;
    vec4 color;
} vs_out;

vec4
uint_color_to_v4(uint color)
{
    float r = float((color >> 24) & 0xff) / 256.0;
    float g = float((color >> 16) & 0xff) / 256.0;
    float b = float((color >> 8) & 0xff) / 256.0;
    float a = float(color & 0xff) / 256.0;
    return(vec4(r, g, b, a));
}

void
main(void)
{
    vs_out.uvs = uvs;
    vs_out.color = uint_color_to_v4(color);
    gl_Position = vec4(position, 0.0, 1.0);
    gl_Position.y *= -1.0;
}
