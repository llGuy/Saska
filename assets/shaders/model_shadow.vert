#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;

layout(binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view[4];
    mat4 shadow_proj[4];
    
    vec4 debug_vector;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
} push_k;

void
main(void)
{
    gl_Position = ubo.shadow_proj[0] * ubo.shadow_view[0] * push_k.model * vec4(vertex_position, 1.0);
}
