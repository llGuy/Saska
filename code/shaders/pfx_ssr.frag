#version 450

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(location = 0) out vec4 final_color;

layout(push_constant) uniform Push_K
{
    vec4 ws_light_direction;
    mat4 view;
    mat4 proj;
} light_info_pk;

layout(binding = 0, set = 0) uniform sampler2D g_final;
layout(binding = 1, set = 0) uniform sampler2D g_position;
layout(binding = 2, set = 0) uniform sampler2D g_normal;

layout(binding = 0, set = 1) uniform samplerCube atmosphere_cubemap;

layout(binding = 0, set = 2) uniform Camera_Transforms
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;
} camera_ubo;

const int num_marches = 30;

vec3 hash33(vec3 p3)
{
    p3 = fract(p3 * vec3(.8, .8, .8));
    p3 += dot(p3, p3.yxz + 19.19);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

vec3 binary_search(inout vec3 dir
		   , inout vec3 hit_coord
		   , inout float depth_difference)
{
    float depth;

    vec4 projected_coord;

    for (int i = 0; i < 8; ++i)
    {
	projected_coord = light_info_pk.proj * vec4(hit_coord, 1.0);
	projected_coord.xy /= projected_coord.w;
	projected_coord.xy = projected_coord.xy * 0.5 + 0.5;

	depth = textureLod(g_position, projected_coord.xy, 2).z;

	depth_difference = hit_coord.z - depth;

	dir *= 0.5;
	if (depth_difference > 0.0) hit_coord += dir;
	else hit_coord -= dir;
    }

    projected_coord = light_info_pk.proj * vec4(hit_coord, 1.0);
    projected_coord.xy /= projected_coord.w;
    projected_coord.xy = projected_coord.xy * 0.5 + 0.5;

    return vec3(projected_coord.xy, depth);
}

vec4 ray_cast(inout vec3 direction
	      , inout vec3 hit_coord
	      , out float depth_difference
	      , out bool success
	      , inout float d)
{
    vec3 original_coord = hit_coord;

    direction *= 0.1;

    vec4 projected_coord;
    float sampled_depth;

    vec3 previous_ray_coord = hit_coord;

    for (int i = 0; i < 30; ++i)
    {
	previous_ray_coord = hit_coord;
	hit_coord += direction;

	projected_coord = light_info_pk.proj * vec4(hit_coord, 1.0);

	projected_coord.xy /= projected_coord.w;
	projected_coord.xy = projected_coord.xy * 0.5 + 0.5;

	sampled_depth = textureLod(g_position, projected_coord.xy, 2).z;

	if (sampled_depth > 1000.0) continue;

	depth_difference = hit_coord.z - sampled_depth;

	if (depth_difference <= 0)
	{
	    vec4 result	= vec4(binary_search(direction,	hit_coord, depth_difference), 0.0);
	    {
		success = true;
		d = texture(g_position, result.xy).z;
		return result;
	    }
	}
    }

    return vec4(projected_coord.xy, sampled_depth, 0.0);
}

vec3 fresnel_schlick(float cos_theta
		     , vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}

vec4 apply_cube_map_reflection(in vec3 vs_eye_vector
			       , in vec3 vs_normal
			       , inout vec4 pixel_color
			       , in vec4 fresnel)
{
    vec3 result = reflect(-normalize(vs_eye_vector), vs_normal);

    mat4 inv_view = inverse(light_info_pk.view);
    vec3 ws_reflect = normalize(vec3(inv_view * vec4(result, 1.0)));

    vec3 ws_eye_vector = normalize(vec3(inv_view * vec4(vs_eye_vector, 0.0)));
    vec3 ws_normal     = normalize(vec3(inv_view * vec4(vs_normal, 0.0)));
	
    vec3 reflect_dir = normalize(reflect(-normalize(ws_eye_vector), normalize(ws_normal)));

    vec4 envi_color = texture(atmosphere_cubemap, reflect_dir);

    pixel_color = pixel_color + (envi_color * 0.2);

    return pixel_color;
}

void
main(void)
{
    vec4 position = (textureLod(g_position, fs_in.uvs, 2));
    vec3 view_position = vec3(position);
    vec4 vnormal = (textureLod(g_normal, fs_in.uvs, 2));
    vec3 view_normal = vnormal.xyz;
    vec4 pixel_color = texture(g_final, fs_in.uvs);
    float metallic = 0.5;

    vec3 original_position = view_position;

    if (view_normal.x > -10.0 && view_normal.y > -10.0 && view_normal.z > -10.0)
    {
	bool hit = false;

	vec3 F0 = vec3(0.04);

	F0 = mix(F0, pixel_color.rgb, metallic);

	vec3 to_camera = normalize(-view_position);
	vec3 to_light = -vec3(light_info_pk.ws_light_direction);
	vec3 halfway = normalize(to_camera + to_light);

	vec4 fresnel = vec4(fresnel_schlick(max(dot(view_normal, to_camera), 0.0), F0), 1.0);

	float ddepth;
	//vec3 world_position = vec3(inverse_view * vec4(view_position, 1.0));
	vec3 jitt = mix(vec3(0.0), vec3(hash33(view_position)), pixel_color.a);
	vec3 ray_dir = normalize(reflect(normalize(original_position), normalize(view_normal)));

	ray_dir = jitt + ray_dir * max(0.1, -view_position.z);
	
	float placeholder;
	// ray cast 
	vec4 coords = ray_cast(ray_dir, view_position, ddepth, hit, placeholder);

	vec2 d_coords = smoothstep(0.2, 0.6, abs(vec2(0.5, 0.5) - coords.xy));
	float factor = (d_coords.x + d_coords.y);
	float edge_factor = clamp(1.0 - factor, 0.0, 1.0);

	vec4 reflected_color = texture(g_final, coords.xy);

	pixel_color = apply_cube_map_reflection(-normalize(original_position + jitt)
						, view_normal
						, pixel_color
						, fresnel);

	//check if is skybox
	vec3 check_skybox = texture(g_normal, coords.xy).xyz;
	bool is_skybox = (check_skybox.x < -10.0 && check_skybox.y < -10.0 && check_skybox.z < -10.0);

	if (hit && !is_skybox)
	{
	    vec3 vs_reflected_dir = normalize(ray_dir);
	    vec3 vs_reflected_point = texture(g_position, coords.xy).xyz;
	    vec3 vs_reflected_point_to_original = normalize(vs_reflected_point - original_position);

	    float dotted = dot(vs_reflected_dir, vs_reflected_point_to_original);
			
	    if (dotted > 0.9999) final_color = mix(pixel_color, reflected_color * fresnel, edge_factor * 0.3);
	    else final_color = pixel_color;
	}
	else final_color = pixel_color;
    }
    else final_color = pixel_color;
}
