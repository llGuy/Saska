#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in VS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;
} gs_in[];

layout(location = 0) out GS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;
} gs_out;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
} push_k;

vec3
get_normal(int i1, int i2, int i3)
{
    vec3 diff_world_pos1 = normalize(gs_in[i2].ws_position - gs_in[i1].ws_position);
    vec3 diff_world_pos2 = normalize(gs_in[i3].ws_position - gs_in[i2].ws_position);
    return(-normalize(cross(diff_world_pos2, diff_world_pos1)));
}

void
main(void)
{
    for (int i = 0; i < 3; ++i)
    {
	gs_out.ws_position = gs_in[i].ws_position;
	gs_out.ws_normal = get_normal(i, (i + 1) % 3, (i + 2) % 3);
	gs_out.color = gs_in[i].color;

	gl_Position = gl_in[i].gl_Position;
	
	EmitVertex();
    }

    EndPrimitive();
}