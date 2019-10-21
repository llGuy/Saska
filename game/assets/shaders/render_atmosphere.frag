#version 450

layout(location = 0) in vec3 in_ms_cubemap_direction;
layout(location = 1) in vec3 in_test;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_sun;

layout(set = 1, binding = 0) uniform samplerCube cubemap_sampler;

void
main(void)
{
    vec4 cubemap_color = texture(cubemap_sampler, in_ms_cubemap_direction);

    out_final = cubemap_color;
    out_albedo = cubemap_color;

    out_position = vec4(-100.0);
    out_normal = vec4(-100.0);
    out_sun = vec4(0, 0, 0, 1);
}
