#version 450

layout(binding = 0) uniform Uniform_Buffer_Object
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
    vec4 color;
} push_k;

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_color;
layout(location = 2) in vec2 uvs;


layout(location = 0) out VS_DATA
{

    vec3 final;
    vec2 uvs;
    vec3 position;
    vec3 normal;

    vec4 shadow_coord;
    
} vs_out;

void
main(void)
{
    vec4 ws_position = push_k.model * vec4(vertex_position, 1.0);
    vec4 vs_position = ubo.view * ws_position;

    gl_Position = ubo.proj * vs_position;
    vs_out.final = push_k.color.rgb;
    vs_out.uvs = uvs;

    vs_out.position = vs_position.xyz;

    vs_out.shadow_coord = ubo.shadow_proj * ubo.shadow_view * ws_position;
}
