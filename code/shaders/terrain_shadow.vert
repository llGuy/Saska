#version 450

layout(location = 0) in vec2 ms_xz;
layout(location = 1) in float ms_y;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
    vec3 color;
} push_k;

void
main(void)
{
    gl_Position = ubo.shadow_proj * ubo.shadow_view * push_k.model * vec4(vec3(ms_xz.x, ms_y, ms_xz.y), 1.0);
}
