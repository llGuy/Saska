#version 450

layout(location = 0) in vec3 vertex_position;

layout(location = 0) out VS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;

    vec3 vs_position;
    vec4 shadow_coord;
} vs_out;

layout(set = 0, binding = 0) uniform camera_information_t
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view[4];
    mat4 shadow_proj[4];

    vec4 debug_vector;
} camera_transforms;

layout(push_constant) uniform push_constant_t
{
    mat4 model;
    vec3 color;
} push_k;

void main(void)
{
    vec3 ms_position = vertex_position;
    vec4 ws_position = push_k.model * vec4(ms_position, 1.0);
    vec4 vs_position = camera_transforms.view * ws_position;

    gl_Position = camera_transforms.proj * vs_position;

    vs_out.ws_position = ws_position.xyz;
    vs_out.ws_normal = normalize(vs_position.xyz);
    vs_out.color = push_k.color;

    vs_out.shadow_coord = camera_transforms.shadow_proj[0] * camera_transforms.shadow_view[0] * ws_position;
    vs_out.vs_position = vs_position.xyz;
}
