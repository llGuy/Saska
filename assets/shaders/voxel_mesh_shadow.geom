#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(set = 0, binding = 0) uniform camera_information_t
{
    mat4 view;
    mat4 proj;

    // Will have to be an array
    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} camera_transforms;

layout(push_constant) uniform push_constant_t
{
    mat4 model;
    vec3 color;
} push_k;

void main(void)
{
    // There are 4 layers in the PSSM
    for (int i = 0; i < 1; ++i)
    {
        gl_Layer = i;
        
        if (gl_Layer == 0)
        {
            for (int v = 0; v < 3; ++v)
            {
                gl_Position = camera_transforms.shadow_proj * camera_transforms.shadow_view * push_k.model * vec4(gl_in[v].gl_Position.xyz, 1.0);

                EmitVertex();
            }
        }
        EndPrimitive();
    }
}