#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 uvs;

layout(location = 0) out VS_DATA
{
    vec3 vs_position;
    vec2 uvs;
    vec4 shadow_coord[4];
} vs_out;

layout(set = 0, binding = 0) uniform camera_transforms_t
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view[4];
    mat4 shadow_proj[4];
} camera;

layout(push_constant) uniform push_k_t
{
    mat4 model;
} push_k;

void main()
{
    vec4 ws_position = push_k.model * vec4(position, 1.0f);
    vec4 vs_position = camera.view * ws_position;

    gl_Position = camera.proj * vs_position;

    vs_out.vs_position = vs_position.xyz;
    vs_out.uvs = uvs;

    for (int i = 0; i < 4; ++i)
    {
        vs_out.shadow_coord[i] = camera.shadow_proj[i] * camera.shadow_view[i] * ws_position;
    }
}
