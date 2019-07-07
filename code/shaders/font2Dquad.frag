#version 450

layout(location = 0) out vec4 final_color;

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(push_constant) uniform PK
{
    vec4 color;
} push_k;

layout(binding = 0, set = 0) uniform sampler2D font_map;

void
main(void)
{
//    final_color = texture(font_map, fs_in.uvs);
final_color = push_k.color;
}
