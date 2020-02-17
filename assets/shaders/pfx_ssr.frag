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
    // Screen space
    vec2 ss_light_position;
} light_info_pk;

layout(binding = 0, set = 0) uniform sampler2D g_final;
layout(binding = 1, set = 0) uniform sampler2D g_position;
layout(binding = 2, set = 0) uniform sampler2D g_normal;
layout(binding = 3, set = 0) uniform sampler2D g_sun;

layout(binding = 0, set = 1) uniform samplerCube atmosphere_cubemap;

const int num_marches = 30;

vec3 hash33(vec3 p3)
{
    p3 = fract(p3 * vec3(.8, .8, .8));
    p3 += dot(p3, p3.yxz + 19.19);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

vec3 binary_search(inout vec3 dir, inout vec3 hit_coord, inout float depth_difference)
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

struct ray_coordinate_t
{
    vec3 vs_position;
    vec3 ss_position;
};

struct ray_binary_search_result_t
{
    bool dot_success;
    bool success;
    vec4 result_coord;
};

ray_binary_search_result_t ray_binary_search(in vec3 vs_direction, in vec3 vs_hit_coord, in vec3 vs_previous, in vec3 vs_original_position)
{
    vec3 original_scaled_direction = vs_direction;

    float cos_theta_original = dot(normalize(vs_direction), normalize(vs_hit_coord - vs_original_position));

    vec3 vs_original_ray_position = vs_original_position;
    vec3 vs_current_ray_position = vs_hit_coord;
    vec3 vs_previous_coord = vs_previous;
    float vs_sampled_depth;

    vec4 projected_coord;
    
    for (int i = 0; i < 16; ++i)
    {
        projected_coord = light_info_pk.proj * vec4(vs_current_ray_position, 1.0);
	projected_coord.xy /= projected_coord.w;
	projected_coord.xy = projected_coord.xy * 0.5 + 0.5;

        vec3 vs_new_projected_ray_position = textureLod(g_position, projected_coord.xy, 2).xyz;
        vs_sampled_depth = vs_new_projected_ray_position.z;

	float depth_difference = vs_current_ray_position.z - vs_new_projected_ray_position.z;

	vs_direction *= 0.5;
        
	if (depth_difference > 0.0)
        {
            vs_current_ray_position += vs_direction;
        }
	else
        {
            vs_current_ray_position -= vs_direction;
        }

        vs_previous_coord = vs_current_ray_position;
    }
    
    float new_cos_theta = dot(normalize(vs_direction), normalize(vs_current_ray_position - vs_original_position));

    // Should have become more precise, if not, then there was a problem
    if (new_cos_theta > cos_theta_original)
    {
        ray_binary_search_result_t result;
        result.result_coord = vec4(projected_coord.xy, vs_sampled_depth, 1.0);
        result.success = true;
        result.dot_success = true;
        return result;
    }
    else
    {        
        ray_binary_search_result_t result;
        result.result_coord = vec4(projected_coord.xy, vs_sampled_depth, 1.0);
        result.success = false;

        if (pow(abs(vs_sampled_depth - vs_current_ray_position.z), 2) < dot(original_scaled_direction, original_scaled_direction))
        {
            result.success = true;
        }

        result.dot_success = false;
        
        return result;
    }
}

vec4 ray_cast(inout vec3 direction, inout vec3 hit_coord, out float depth_difference, out bool success, inout float d)
{
    vec3 original_coord = hit_coord;
    vec3 previous_coord = original_coord;

    direction *= 0.04;

    vec4 projected_coord;
    float sampled_depth;

    vec3 previous_ray_coord = hit_coord;

    for (int i = 0; i < 40; ++i)
    {
	hit_coord += direction;

	projected_coord = light_info_pk.proj * vec4(hit_coord, 1.0);

	projected_coord.xy /= projected_coord.w;
	projected_coord.xy = projected_coord.xy * 0.5 + 0.5;

	sampled_depth = textureLod(g_position, projected_coord.xy, 2).z;

	depth_difference = hit_coord.z - sampled_depth;

        // The sampled depth is further away from the camera than the hit coordinate
	if (depth_difference <= 0)
	{
            // Need to check with binary search if hit coord is accurate
            ray_binary_search_result_t result = ray_binary_search(direction, hit_coord, previous_ray_coord, original_coord);
            if (result.success)
	    {
                // Fill paramter
		success = true;
		return result.result_coord;
	    }
	}
        
	previous_ray_coord = hit_coord;

        if (i > 30)
        {
            direction *= 3.0;
        }
    }

    return vec4(projected_coord.xy, sampled_depth, 0.0);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}

vec4 apply_cube_map_reflection(in vec3 vs_eye_vector, in vec3 vs_normal, inout vec4 pixel_color, in vec4 fresnel)
{
    vec3 result = reflect(-normalize(vs_eye_vector), vs_normal);

    mat4 inv_view = inverse(light_info_pk.view);
    vec3 ws_reflect = normalize(vec3(inv_view * vec4(result, 1.0)));

    vec3 ws_eye_vector = normalize(vec3(inv_view * vec4(vs_eye_vector, 0.0)));
    vec3 ws_normal     = normalize(vec3(inv_view * vec4(vs_normal, 0.0)));
	
    vec3 reflect_dir = normalize(reflect(-normalize(ws_eye_vector), normalize(ws_normal)));

    vec4 envi_color = texture(atmosphere_cubemap, reflect_dir);

    pixel_color = pixel_color + (envi_color * 0.03);

    return pixel_color;
}

void main(void)
{
    final_color = texture(g_final, fs_in.uvs);
    vec4 position = (textureLod(g_position, fs_in.uvs, 2));
    vec3 view_position = vec3(position);
    vec4 vnormal = (textureLod(g_normal, fs_in.uvs, 2));
    vec3 view_normal = vnormal.xyz;
    vec4 pixel_color = texture(g_final, fs_in.uvs);
    float metallic = 0.5;

    vec3 original_position = view_position;

    if (view_normal.x > -10.0 && view_normal.y > -10.0 && view_normal.z > -10.0)
    {
	/*bool hit = false;

	vec3 F0 = vec3(0.04);

	F0 = mix(F0, pixel_color.rgb, metallic);

	vec3 to_camera = normalize(view_position);
	vec3 to_light = -vec3(normalize(light_info_pk.ws_light_direction));
	vec3 halfway = normalize(to_camera + to_light);

	vec4 fresnel = vec4(fresnel_schlick(max(dot(view_normal, to_camera), 0.0), F0), 1.0);

	float ddepth;
	//vec3 world_position = vec3(inverse_view * vec4(view_position, 1.0));
	vec3 jitt = mix(vec3(0.0), vec3(hash33(view_position)), pixel_color.a * 0.8);
        //vec3 jitt = vec3(0.0);
	vec3 ray_dir = normalize(reflect(normalize(original_position), normalize(view_normal)));

	ray_dir = jitt + ray_dir * max(0.1, -view_position.z);
	
	float placeholder;*/
	// ray cast
        // Dont need the below
        /*vec4 coords = ray_cast(ray_dir, view_position, ddepth, hit, placeholder);

	vec2 d_coords = smoothstep(0.2, 0.6, abs(vec2(0.5, 0.5) - coords.xy));
	float factor = (d_coords.x + d_coords.y);
	float edge_factor = clamp(1.0 - factor, 0.0, 1.0);

	vec4 reflected_color = texture(g_final, coords.xy);*/
        // Dont need the aboe

        // Need to keep these below
        //pixel_color = apply_cube_map_reflection(-normalize(original_position + jitt), view_normal, pixel_color, fresnel);

        final_color = pixel_color;
        
        // Need to keep these above

	//check if is skybox
        // Dont need to below
	/*vec3 check_skybox = texture(g_normal, coords.xy).xyz;
	bool is_skybox = (check_skybox.x < -10.0 && check_skybox.y < -10.0 && check_skybox.z < -10.0);

	if (hit && !is_skybox)
	{
	    vec3 vs_reflected_dir = normalize(ray_dir);
	    vec3 vs_reflected_point = texture(g_position, coords.xy).xyz;
	    vec3 vs_reflected_point_to_original = normalize(vs_reflected_point - original_position);

	    float dotted = dot(vs_reflected_dir, vs_reflected_point_to_original);

            //final_color = mix(pixel_color, reflected_color * fresnel, edge_factor * 0.3);

            //final_color = mix(pixel_color, reflected_color * fresnel, edge_factor * 0.3);
            
	    if (dotted > 0.9999) final_color = mix(pixel_color, reflected_color * fresnel, edge_factor * 0.3);
	    else final_color = pixel_color;
	}
	else final_color = pixel_color;*/
        // Dont need the above
    }
    else final_color = pixel_color;

    //final_color = 
    final_color.a = 1.0;

    const int SAMPLES = 50;
    const float DENSITY = 1.0;
    const float DECAY = 0.9;
    const float WEIGHT = 0.9;

    vec2 blur_vector = (light_info_pk.ss_light_position - fs_in.uvs) * (1.0 / float(SAMPLES));
    vec2 current_uvs = fs_in.uvs;

    float illumination_decay = 1.0;
    
    for (int i = 0; i < SAMPLES; ++i)
    {
        current_uvs += blur_vector;

        vec4 current_color = texture(g_sun, current_uvs);

        current_color *= illumination_decay * WEIGHT;

        final_color += current_color;

        illumination_decay *= DECAY;
    }
}
