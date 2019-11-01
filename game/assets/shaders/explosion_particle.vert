#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_velocity;
layout(location = 2) in vec3 vertex_up_vector;
layout(location = 3) in float vertex_life;
layout(location = 4) in float vertex_size;

layout(location = 0) out VS_DATA
{
    mat4 projection_matrix;
    vec3 projected_coords;
    vec3 vs_position;
    bool dead;
    vec2 before_uvs;
    vec2 after_uvs;
    float lerp;
    float width;
    float height;
} vs_out;

layout(set = 0, binding = 0) uniform camera_transforms_t
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} camera_transforms;

layout(push_constant) uniform explosion_info_t
{
    float max_life_length;
    uint atlas_width;
    uint atlas_height;
    uint num_stages;
} explosion_info;

const vec2 QUAD_VERTICES[] = vec2[](vec2(-1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1));
const vec2 QUAD_UVS[] = vec2[](vec2(0, 0), vec2(0, 1), vec2(1, 0), vec2(1, 1));

void main(void)
{
    if (vertex_life > explosion_info.max_life_length)
    {
        vs_out.dead = true;
        return;
    }

    vs_out.dead = false;
    
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

    vec4 vs_position = camera_transforms.view * model_matrix * vec4(ms_vertex, 1);
    gl_Position = camera_transforms.proj * vs_position;

    vs_out.vs_position = vs_position.xyz;
    vs_out.projected_coords = vec4(gl_Position / gl_Position.w).xyz;
    vs_out.projection_matrix = camera_transforms.proj;
    
    vs_out.width = float(explosion_info.atlas_width);
    vs_out.height = float(explosion_info.atlas_height);
    vs_out.before_uvs = QUAD_UVS[gl_VertexIndex];
    vs_out.after_uvs = QUAD_UVS[gl_VertexIndex];

    float scaled = (vertex_life / explosion_info.max_life_length) * float(explosion_info.num_stages);
    float before = floor(scaled);
    float after = ceil(scaled);

    vec2 before_v2 = vec2(float(uint(before) % explosion_info.atlas_width), floor(before / float(explosion_info.atlas_height))) / vec2(vs_out.width, vs_out.height);
    vec2 after_v2 = vec2(float(uint(after) % explosion_info.atlas_width), floor(after / float(explosion_info.atlas_height))) / vec2(vs_out.width, vs_out.height);

    vs_out.before_uvs /= vec2(vs_out.width, vs_out.height);
    vs_out.after_uvs /= vec2(vs_out.width, vs_out.height);

    vs_out.before_uvs += before_v2;
    vs_out.after_uvs += after_v2;

    vs_out.lerp = (scaled - before) / (after - before);
}
