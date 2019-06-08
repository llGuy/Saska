#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_color;
layout(location = 2) in vec2 uvs;

layout(binding = 0) uniform Uniform_Buffer_Object
{
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 shadow_proj;
    mat4 shadow_view;
    
    bool render_shadow;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
} push_k;

void
main(void)
{
    //    gl_Position = ubo.shadow_proj * ubo.shadow_view * ubo.model * vec4(vertex_position, 1.0);
    gl_Position = ubo.proj * ubo.view * push_k.model * vec4(vertex_position, 1.0);
}
