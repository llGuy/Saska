#version 450

layout(location = 0) in vec3 vertex_position;

layout(set = 0, binding = 0) uniform camera_information_t
{
    mat4 view;
    mat4 projection;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} camera_transforms;

layout(push_constant) uniform push_constant_t
{
    mat4 model;
} push_k;

layout(location = 0) out VS_DATA
{
    vec4 color;
} vs_out;

void main(void)
{
    gl_Position = camera_transforms.projection * camera_transforms.view * push_k.model * vec4(vertex_position, 1.0);

    vs_out.color = vec4(0.2, 0.2, 0.2, 1.0);

    gl_PointSize = 3.0;
}
