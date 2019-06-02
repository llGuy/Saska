#version 450

layout(location = 0) out vec4 final_color;

layout(location = 0) in vec2 in_uvs;

layout(set = 0, binding = 0) uniform sampler2D tex;

void
main(void)
{
    final_color = vec4(texture(tex, in_uvs).r);
}
