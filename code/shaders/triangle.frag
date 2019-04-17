#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uvs;
layout(location = 2) in vec3 frag_position;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;

layout(set = 0, binding = 1) uniform sampler2D texture_sampler;

void main(void)
{
    out_final = vec4(frag_color, 1.0);

    out_albedo = vec4(frag_color, 1.0);

    out_albedo = vec4(frag_color, 1.0);

    //    out_position = vec4(1.0f);

    /*out_normal = vec4(frag_position, 1.0f);*/
}
