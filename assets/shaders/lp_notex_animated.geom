#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in VS_DATA
{

    vec3 final;
    vec3 position;
    vec3 normal;
    vec4 shadow_coord;
    
} gs_in[];

layout(location = 0) out GS_DATA
{

    vec3 final;
    vec3 position;
    vec3 normal;
    vec4 shadow_coord;
    
} gs_out;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view[4];
    mat4 shadow_proj[4];
    vec4 debug_vector;
} ubo;

layout(push_constant) uniform Push_Constants
{
    
    mat4 model;
    
} push_k;

vec3
get_normal(int i1, int i2, int i3)
{
    vec3 vs_diff_pos1 = normalize(gs_in[i2].position - gs_in[i1].position);
    vec3 vs_diff_pos2 = normalize(gs_in[i3].position - gs_in[i2].position);
    return(normalize(cross(vs_diff_pos2, vs_diff_pos1)));
}

void
main(void)
{
    vec3 normal = -get_normal(0, 1, 2);
    
    for (int i = 0; i < 3; ++i)
    {
	gs_out.position = gs_in[i].position;
	gs_out.normal = normal;
	gs_out.final = gs_in[i].final;

        for (int c = 0; c < 4; ++c)
        {
            gs_out.shadow_coord[c] = gs_in[i].shadow_coord[c];
        }

	gl_Position = gl_in[i].gl_Position;

	EmitVertex();
    }

    EndPrimitive();
}
