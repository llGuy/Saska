#version 450

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 ts_to_ws_terrain_model;
    vec4 color;
    vec4 ts_center_position;
    // center first
    float ts_heights[8];

    vec4 new_terrain_pointer[3];
} push_k;

layout(location = 0) out VS_OUT
{
    vec4 color;
} vs_out;

const vec2 VTX[] = vec2[]( vec2(0, 0)
			   , vec2(-1, -1)
			   , vec2(0, 0)
			   , vec2(+1, -1)
			   , vec2(0, 0)
			   , vec2(+1, +1)
			   , vec2(0, 0)
			   , vec2(-1, +1) );

#define NEW_POINTER

void
main(void)
{
#if defined(NEW_POINTER)

    gl_Position = ubo.proj * ubo.view * push_k.new_terrain_pointer[gl_VertexIndex];
    vs_out.color = push_k.color;
    
#else
    int vertex_index = gl_VertexIndex;

    // ---- calculate position of the lines ----
    vec3 ts_line_point_position;
    if (push_k.ts_heights[vertex_index] > -0.1f)
    {
	ts_line_point_position = vec3(VTX[vertex_index].x, push_k.ts_heights[vertex_index], VTX[vertex_index].y);
    }
    else
    {
	ts_line_point_position = vec3(VTX[0].x, push_k.ts_heights[0], VTX[0].y);
    }
    
    ts_line_point_position += vec3(push_k.ts_center_position.x, 0.01, push_k.ts_center_position.z);
    gl_Position = ubo.proj * ubo.view * push_k.ts_to_ws_terrain_model * vec4(ts_line_point_position, 1.0);
    
    vs_out.color = push_k.color;
#endif
}
