#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in VS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;

    vec3 vs_position;
    vec4 shadow_coord;
} gs_in[];

layout(location = 0) out GS_DATA
{
    vec3 color;
    vec3 ws_position;
    vec3 ws_normal;

    vec3 vs_position;
    vec4 shadow_coord;
} gs_out;

vec3 get_normal(int i1, int i2, int i3)
{
    vec3 diff_world_pos1 = normalize(gs_in[i2].ws_position - gs_in[i1].ws_position);
    vec3 diff_world_pos2 = normalize(gs_in[i3].ws_position - gs_in[i2].ws_position);
    return(normalize(cross(diff_world_pos2, diff_world_pos1)));
}

void main(void)
{
    vec3 normal = -vec3(vec4(get_normal(0, 1, 2), 0.0));
    
    for (int i = 0; i < 3; ++i)
    {
	gs_out.ws_position = gs_in[i].ws_position;
	gs_out.ws_normal = normal;
	gs_out.color = gs_in[i].color;
	
	gs_out.vs_position = gs_in[i].vs_position;
	gs_out.shadow_coord = gs_in[i].shadow_coord;

	gl_Position = gl_in[i].gl_Position;
	
	EmitVertex();
    }

    EndPrimitive();
}
