#version 450

layout(push_constant) uniform Push_Constants
{
    mat4 model_matrix;
    vec4 vertices[8];
    vec4 color;
    
} push_k;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} ubo;

layout(location = 0) out VS_DATA
{
    vec3 color;
} vs_out;

int indices[] = int[] ( 0, 1
			, 1, 2
			, 2, 3
			, 3, 0
			, 0, 4
			, 1, 5
			, 2, 6
			, 3, 7
			, 4, 5
			, 5, 6
			, 6, 7
			, 7, 4);

void
main(void)
{
    vec4 ws_position = push_k.model_matrix * push_k.vertices[ indices[gl_VertexIndex] ];
    vec4 vs_position = ubo.view * ws_position;
    gl_Position = ubo.proj * vs_position;

    vs_out.color = push_k.color.rgb;
}
