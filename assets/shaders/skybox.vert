#version 450

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 out_dir;

layout(binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view_matrix;
    mat4 projection_matrix;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
    mat4 view_rotate;
} push_k;

void
main(void)
{
    
    gl_Position = ubo.projection_matrix * push_k.view_rotate * vec4(in_position, 1.0);
}
