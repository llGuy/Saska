#version 450

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(location = 0) out vec4 final_color;

void main(void)
{
    final_color = vec4(fs_in.uvs, 0, 1);
}
