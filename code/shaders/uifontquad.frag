#version 450

layout(location = 0) out vec4 final_color;

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(binding = 0, set = 0) uniform sampler2D fontmap;

void
main(void)
{
    final_color = texture(fontmap, fs_in.uvs);
}
