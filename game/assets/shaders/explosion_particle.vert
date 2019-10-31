#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_velocity;
layout(location = 2) in vec3 vertex_up_vector;
layout(location = 3) in float vertex_life;
layout(location = 4) in float vertex_size;

layout(location = 0) out VS_DATA
{
    vec2 uvs;
} vs_out;

layout(set = 0, binding = 0) uniform camera_transforms_t
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} camera_transforms;

const vec2 QUAD_VERTICES[] = vec2[](vec2(-1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1));
const vec2 QUAD_UVS[] = vec2[](vec2(0, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1));

void main(void)
{
    vec3 ms_vertex = vec3(QUAD_VERTICES[gl_VertexIndex]  * vertex_size, 0);
    
    mat4 model_matrix = mat4(0);
    model_matrix[0][0] = 1;
    model_matrix[1][1] = 1;
    model_matrix[2][2] = 1;
    model_matrix[3][3] = 1;
    model_matrix[3][0] = vertex_position.x;
    model_matrix[3][1] = vertex_position.y;
    model_matrix[3][2] = vertex_position.z;

    mat3 rotation_part = mat3(camera_transforms.view);
    rotation_part = transpose(rotation_part);

    for (int i = 0; i < 3; ++i)
    {
	for (int j = 0; j < 3; ++j)
	{
	    model_matrix[i][j] = rotation_part[i][j];
	}
    }
    
    gl_Position = camera_transforms.proj * camera_transforms.view * model_matrix * vec4(ms_vertex, 1);

    vs_out.uvs = QUAD_UVS[gl_VertexIndex];
}
