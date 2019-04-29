#version 450

layout(location = 0) in vec3 in_ms_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_uvs;

layout(location = 0) out vec3 out_ms_cubemap_direction;
layout(location = 1) out vec3 out_test;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform Push_K
{
    mat4 model_matrix;
} push_k;

void
main(void)
{
    mat4 view_no_translation = ubo.view;
    view_no_translation[3][0] = 0.0;
    view_no_translation[3][1] = 0.0;
    view_no_translation[3][2] = 0.0;
    
    gl_Position = ubo.proj * view_no_translation * push_k.model_matrix * vec4(in_ms_position * 100, 1.0);
    out_ms_cubemap_direction = in_ms_position;

    out_test = in_color;
}
