#version 450

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;

layout(location = 0) in FS_IN
{
    vec4 color;
} fs_in;

void
main(void)
{
    out_final = vec4(0.0, 0.0, 0.0, 1.0);
    out_albedo = vec4(1.0);
    out_position = vec4(-100.0);
    out_normal = vec4(-100.0);
}
