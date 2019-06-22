#version 450

layout(location = 0) in vec2 ms_xz;
layout(location = 1) in float ms_y;

/*layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_color;
layout(location = 2) in vec2 uvs;*/

layout(location = 0) out VS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;

    vec3 vs_position;
    vec4 shadow_coord;
} vs_out;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 shadow_proj;
    mat4 shadow_view;

    mat4 shadow_bias;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
    vec3 color;
} push_k;

void
main(void)
{
    // ---- create the 3D mesh position ----
    vec3 ms_position = vec3(ms_xz.x, ms_y, ms_xz.y);
    vec4 ws_position = push_k.model * vec4(ms_position, 1.0);
    vec4 vs_position = ubo.view * ws_position;

    gl_Position = ubo.proj * vs_position;

    vs_out.ws_position = vs_position.xyz;
    vs_out.ws_normal = normalize(vs_position.xyz);
    vs_out.color = push_k.color;

    vs_out.shadow_coord = ubo.shadow_proj * ubo.shadow_view * ws_position;
    vs_out.vs_position = vs_position.xyz;
}
