#version 450

layout(location = 0) in vec3 vertex_position;

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
    gl_Position = camera_transforms.shadow_proj[0] * camera_transforms.shadow_view[0] * push_k.model * vec4(vertex_position, 1.0);
}
