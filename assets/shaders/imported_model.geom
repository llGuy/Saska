#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) out VS_DATA
{
    vec3 vs_position;
    vec2 uvs;
    vec4 shadow_coord[4];
} vs_out;

layout(location = 0) out GS_DATA
{
    vec3 vs_position;
    vec3 vs_normal;
    vec2 uvs;

    vec4 shadow_coord[4];
} gs_out;

vec3 get_normal(int i1, int i2, int i3)
{
    vec3 vs_diff_pos1 = normalize(gs_in[i2].vs_position - gs_in[i1].vs_position);
    vec3 vs_diff_pos2 = normalize(gs_in[i3].vs_position - gs_in[i2].vs_position);
    return(normalize(cross(vs_diff_pos2, vs_diff_pos1)));
}

void main()
{
    vec3 normal = -get_normal(0, 1, 2);
    
    for (int i = 0; i < 3; ++i)
    {
	gs_out.vs_position = gs_in[i].vs_position;
	gs_out.vs_normal = normal;
        gs_out.uvs = gs_in[i].uvs;

        for (int c = 0; c < 4; ++c)
        {
            gs_out.shadow_coord[c] = gs_in[i].shadow_coord[c];
        }

	gl_Position = gl_in[i].gl_Position;

	EmitVertex();
    }

    EndPrimitive();
}
