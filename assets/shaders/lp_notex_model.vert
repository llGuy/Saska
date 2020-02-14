#version 450

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
    vec4 color;

    float roughness;
    float metalness;
} push_k;

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;

layout(location = 0) out VS_DATA
{
    vec3 final;
    vec3 position;
    vec3 normal;
    vec4 shadow_coord;
    
} vs_out;

void
main(void)
{
    vec4 ws_position = push_k.model * vec4(vertex_position, 1.0);
    vec4 vs_position = ubo.view * ws_position;
    vec3 real_normal = vertex_normal;
    // I don't know why blender flips the y?
    real_normal.y *= -1.0;
    vec4 vs_normal = ubo.view * vec4(normalize(real_normal), 0.0);

    gl_Position = ubo.proj * vs_position;
    vs_out.final = push_k.color.rgb;

    vs_out.position = vs_position.xyz;
    vs_out.normal = vs_normal.xyz;

    vs_out.shadow_coord = ubo.shadow_proj[0] * ubo.shadow_view[0] * ws_position;
}
