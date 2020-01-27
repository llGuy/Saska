#version 450

layout(location = 0) out vec4 out_final;
//layout(location = 1) out vec4 out_albedo;
//layout(location = 2) out vec4 out_position;
//layout(location = 3) out vec4 out_normal;

layout(location = 0) in vec2 in_uvs;

layout(set = 0, binding = 0) uniform sampler2D tex;

float linearize_depth(float depth)
{
    float n = 1.0; // camera z near
    float f = 100.0; // camera z far
    float z = depth;
    return (2.0 * n) / (f + n - depth * (f - n));	
}

float linearize(float d,float zNear,float zFar)
{
    float z_n = 2.0 * d - 1.0;
    return 2.0 * zNear * zFar / (zFar + zNear - z_n * (zFar - zNear));
}

void
main(void)
{
    float d = texture(tex, in_uvs).r;

    out_final = vec4(d);
    //    out_position = vec4(-100.0);
    //    out_normal = vec4(-100.0);
    //    final_color = d;
//    final_color = vec4(1 - d) * 10;
}

