#version 450

layout(location = 0) in GS_DATA
{

    vec3 final;
    vec3 position;
    vec3 normal;
    vec4 shadow_coord;
    
} fs_in;


layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_sun;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
    vec4 color;

    float roughness;
    float metalness;
} push_k;

layout(set = 1, binding = 0) uniform sampler2D shadow_map;

void
set_roughness(float v)
{
    out_normal.a = v;
}

void
set_metalness(float v)
{
    out_position.a = v;
}

const float MAP_SIZE = 4000.0;
const float PCF_COUNT = 1.0;
const float TRANSITION_DISTANCE = 20.0;
const float SHADOW_DISTANCE = 1000.0;

float get_shadow_light_factor(float dist, in vec4 shadow_coord)
{
    float total_texels = (PCF_COUNT * 2.0f + 1.0f) * (PCF_COUNT * 2.0f + 1.0f);

    dist = dist - (SHADOW_DISTANCE - TRANSITION_DISTANCE);
    dist = dist / TRANSITION_DISTANCE;
    dist = clamp(1.0 - dist, 0.0, 1.0);

    float texel_size = 1.0f / MAP_SIZE;
    float total = 0.0f;

    vec3 shadow_space_pos = shadow_coord.xyz / shadow_coord.w;
    shadow_space_pos.xy = shadow_space_pos.xy * 0.5 + 0.5;
    
    if (shadow_space_pos.z > -1.0 && shadow_space_pos.z < 1.0
	&& shadow_space_pos.x > 0.0 && shadow_space_pos.x < 1.0
	&& shadow_space_pos.y > 0.0 && shadow_space_pos.y < 1.0)
    {
	for (int x = int(-PCF_COUNT); x <= int(PCF_COUNT); ++x)
	{
	    for (int y = int(-PCF_COUNT); y <= int(PCF_COUNT); ++y)
	    {
		float object_nearest_light = texture(shadow_map, shadow_space_pos.xy + vec2(x, y) * texel_size).x;
		if (shadow_space_pos.z - 0.005 > object_nearest_light)
		{
		    total += 0.8f;
		}
	    }
	}
	total /= total_texels;
    }

    float light_factor = 1.0f - (total * dist);

    return light_factor;
}

void main(void)
{
    float shadow_factor = get_shadow_light_factor(length(fs_in.position), fs_in.shadow_coord);

    out_final = vec4(0.0, 0.0, 0.0, 1.0);
    out_albedo = vec4(fs_in.final, shadow_factor);

    out_position = vec4(fs_in.position, 1.0);

    out_normal = vec4(fs_in.normal, 1.0);

    out_sun = vec4(0, 0, 0, 1);

    set_roughness(push_k.roughness);
    set_metalness(push_k.metalness);
}
