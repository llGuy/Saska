#version 450

layout(location = 0) in VS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;
} fs_in;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;

layout(set = 1, binding = 0) uniform samplerCube cube_sampler;

void
set_roughness(float v)
{
    out_normal.a = v;
}

void
set_metalness(float v)
{
    out_position.a = v;
}

void
main(void)
{
    out_final = vec4(fs_in.color, 1.0);
    out_albedo = vec4(fs_in.color, 1.0);
    out_position = vec4(fs_in.ws_position, 1.0);
    out_normal = vec4(fs_in.ws_normal, 1.0);

    set_roughness(0.7);
    set_metalness(0.3);
}
