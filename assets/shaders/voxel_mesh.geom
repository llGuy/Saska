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
    vec3 ws_position;
    vec3 vs_normal;

    vec3 vs_position;
    vec4 shadow_coord;

    vec3 color;
    float roughness;
    float metalness;
} gs_out;

struct voxel_color_beacon_t
{
    vec4 ws_position;
    vec4 color;
    float reach; // Radius
    float roughness;
    float metalness;
    float power;
};

layout(set = 2, binding = 0) uniform voxel_color_beacons_t
{
    voxel_color_beacon_t default_voxel_color;
    voxel_color_beacon_t voxel_color_beacons[50];
    int beacon_count;
} voxel_colors;

vec3 get_normal(int i1, int i2, int i3)
{
    vec3 diff_world_pos1 = normalize(gs_in[i2].vs_position - gs_in[i1].vs_position);
    vec3 diff_world_pos2 = normalize(gs_in[i3].vs_position - gs_in[i2].vs_position);
    return(normalize(cross(diff_world_pos2, diff_world_pos1)));
}

void main(void)
{
    vec3 normal = -vec3(vec4(get_normal(0, 1, 2), 0.0));


    // Calculate the color of the triangle
    vec3 color = voxel_colors.default_voxel_color.color.rgb;
    float roughness = voxel_colors.default_voxel_color.roughness;
    float metalness = voxel_colors.default_voxel_color.metalness;

    float div = 1.0;

    vec3 ws_position_of_triangle = gs_in[0].ws_position;
    for (int i = 0; i < voxel_colors.beacon_count; ++i)
    {
        vec3 diff = ws_position_of_triangle - voxel_colors.voxel_color_beacons[i].ws_position.xyz;
        
        //float d_div = (voxel_colors.voxel_color_beacons[i].reach * voxel_colors.voxel_color_beacons[i].reach) / dot(diff, diff);
        float d_div = (voxel_colors.voxel_color_beacons[i].reach * voxel_colors.voxel_color_beacons[i].reach * voxel_colors.voxel_color_beacons[i].power) / dot(diff, diff);

        div += d_div;

        color += d_div * voxel_colors.voxel_color_beacons[i].color.rgb;
        roughness += d_div * voxel_colors.voxel_color_beacons[i].roughness;
        metalness += d_div * voxel_colors.voxel_color_beacons[i].metalness;
    }

    color /= div;
    roughness /= div;
    metalness /= div;
    
    
    for (int i = 0; i < 3; ++i)
    {
	gs_out.ws_position = gs_in[i].ws_position;
	gs_out.vs_normal = normal;
	
	gs_out.vs_position = gs_in[i].vs_position;
	gs_out.shadow_coord = gs_in[i].shadow_coord;

        gs_out.color = color;
        gs_out.roughness = roughness;
        gs_out.metalness = metalness;

	gl_Position = gl_in[i].gl_Position;
	
	EmitVertex();
    }

    EndPrimitive();
}
