#version 450

layout(location = 0) out VS_DATA
{
    vec2 uvs;
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
    vec3 ws_light_direction;
} push_k;

const vec3 QUAD_POSITIONS[4] = vec3[4](vec3(-1, -1, 0),
                                       vec3(-1, 1, 0),
                                       vec3(1, -1, 0),
                                       vec3(1, 1, 0));
const vec2 QUAD_UVS[4] = vec2[4](vec2(0, 0),
                                 vec2(0, 1),
                                 vec2(1, 0),
                                 vec2(1, 1));

void main(void)
{
    vec3 vertex_position = QUAD_POSITIONS[gl_VertexIndex];
    vec2 uvs = QUAD_UVS[gl_VertexIndex];

    mat4 scale = mat4(0);
    scale[0][0] = push_k.model[0][0];
    scale[1][1] = push_k.model[1][1];
    scale[2][2] = push_k.model[2][2];
    scale[3][3] = push_k.model[3][3];

    mat4 view_matrix_no_translation = camera_transforms.view;
    mat4 model_transpose_rotation = push_k.model;

    mat3 rotation_part = mat3(view_matrix_no_translation);
    rotation_part = transpose(rotation_part);

    for (int i = 0; i < 3; ++i)
    {
	for (int j = 0; j < 3; ++j)
	{
	    model_transpose_rotation[i][j] = rotation_part[i][j];
	}
    }

    view_matrix_no_translation[3][0] = 0;
    view_matrix_no_translation[3][1] = 0;
    view_matrix_no_translation[3][2] = 0;

    gl_Position = camera_transforms.proj * view_matrix_no_translation * model_transpose_rotation * scale * vec4(vertex_position * 80, 1.0);

    vs_out.uvs = uvs;
}
