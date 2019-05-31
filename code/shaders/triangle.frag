#version 450

layout(location = 0) in GS_DATA
{

    vec3 final;
    vec3 uvs;
    vec3 position;
    vec3 normal;
    
} fs_in;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;

layout(set = 1, binding = 0) uniform samplerCube cube_sampler;

void main(void)
{
    out_final = vec4(fs_in.final, 1.0);

    out_albedo = vec4(fs_in.final, 1.0);

    out_position = vec4(vec3(0.5), 1.0);

    out_normal = vec4(fs_in.normal, 1.0);
}
