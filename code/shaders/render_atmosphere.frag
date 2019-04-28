#version 450

layout(location = 0) in vec3 in_ms_cubemap_direction;
layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;

layout(set = 1, binding = 0) uniform samplerCube cubemap_sampler;

void
main(void)
{
    vec4 cubemap_color = texture(cubemap_sampler, in_ms_cubemap_direction);

    out_final = cubemap_color;
    out_albedo = out_albedo;

    out_final = cubemap_color;
    out_albedo = cubemap_color;
}
