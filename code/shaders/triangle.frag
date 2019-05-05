#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uvs;
layout(location = 2) in vec3 frag_position;
layout(location = 3) in vec3 frag_normal;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;

layout(set = 1, binding = 0) uniform samplerCube cube_sampler;

void main(void)
{
    out_final = vec4(frag_color, 1.0);

    out_albedo = vec4(frag_color, 1.0);

    out_position = vec4(vec3(0.5), 1.0);

    out_normal = vec4(frag_normal, 1.0);
}
