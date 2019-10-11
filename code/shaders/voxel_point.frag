#version 450

layout(location = 0) in VS_DATA
{
    vec4 color;
} fs_in;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;

void main(void)
{
    out_final = fs_in.color;
    out_albedo = fs_in.color;
    out_position = vec4(-100.0);
    out_normal = vec4(-100.0);
}


