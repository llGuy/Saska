/* fragment shader simply shades a flat quad (that fills screen) */

#version 450

layout(location = 0) in vec3 in_cube_face_direction;
layout(location = 0) out vec4 out_color;

// may contain moroe data in the future to paramterise the atmosphere colors...
layout(push_constant) uniform Push_K
{
    mat4 inverse_projection;
    vec4 ws_light_direction;
    vec2 viewport;
} push_k;

mat4
look_at(vec3 eye
	, vec3 center
	, vec3 up)
{
    vec3 f = vec3(normalize(center - eye));
    vec3 s = vec3(normalize(cross(f, up)));
    vec3 u = vec3(cross(s, f));

    mat4 m = mat4(1.0f);
    m[0][0] = s.x;
    m[1][0] = s.y;
    m[2][0] = s.z;
    m[0][1] = u.x;
    m[1][1] = u.y;
    m[2][1] = u.z;
    m[0][2] =-f.x;
    m[1][2] =-f.y;
    m[2][2] =-f.z;
    m[3][0] =-dot(s, eye);
    m[3][1] =-dot(u, eye);
    m[3][2] = dot(f, eye);
    return m;
}

float
square(float a)
{
    return(a * a);
}

/*G < 0 --> light is scattered a lot 
  Mie : -0.75 > G > -0.999 */
float
phase(float difference_light_dir_view_dir
      , const float G)
{
    float left = (3.0 * (1.0 - square(G))) / (2.0 * (2.0 + square(G)));
    float right = (1.0 + square(difference_light_dir_view_dir)) / (pow(1.0 + square(G) - 2.0 * G * difference_light_dir_view_dir, 1.5));
    return left * right;	
}

const float RAYLEIGH_G	= -0.01;
const float MIE_G	= -0.8;

float
calculate_atmosphere_depth_at_dir(vec3 ws_camera_position
				  , vec3 ws_camera_view_dir)
{
    float a = dot(ws_camera_view_dir, ws_camera_view_dir);
    float b = 2.0 * dot(ws_camera_view_dir, ws_camera_position);
    float c = dot(ws_camera_position, ws_camera_position) - 1.0;
    float det = b * b - 4.0 * a * c;
    float sqrt_det = sqrt(det);
    float q = (-b - sqrt_det)/2.0;
    float t1 = c/q;
    return t1;
}

const float SURFACE_HEIGHT = 0.8;
const uint SAMPLE_COUNT = 5;
const vec3 AIR_COLOR = vec3(0.18867780, 0.49784429, 0.3616065);

vec3
absorb_light_from_sun_at_ray_position(float ray_distance
				      , vec3 intensity
				      , float absorb_factor)
{
    // further away the ray goes, smaller the intensity is
    return(intensity - intensity * pow(AIR_COLOR, vec3(absorb_factor / ray_distance)));
}

void
main(void)
{
    mat3 inverse_view_rotate = inverse(mat3(look_at(vec3(0.0f)
						    , in_cube_face_direction
						    , vec3(0.0f, 1.0f, 0.0f))));
    
    vec2 ds_view_dir = gl_FragCoord.xy / push_k.viewport;
    if (gl_Layer == 2 || gl_Layer == 3)
    {
	ds_view_dir.xy = 1 - ds_view_dir.xy;
    }
    ds_view_dir.y = 1.0 - ds_view_dir.y;
    ds_view_dir -= vec2(0.5f);
    ds_view_dir *= 2.0f;
    
    vec3 vs_view_dir = (push_k.inverse_projection * vec4(ds_view_dir, 0.0f, 1.0f)).xyz;
    
    vec3 ws_view_dir = normalize(mat3(inverse_view_rotate) * vs_view_dir);

    float difference_light_dir_view_dir = dot(ws_view_dir, push_k.ws_light_direction.xyz);
    float mie_scattering_factor		= phase(difference_light_dir_view_dir, MIE_G);
    float rayleigh_scattering_factor	= phase(difference_light_dir_view_dir, RAYLEIGH_G);

    vec3 ws_camera_position = vec3(0.0, SURFACE_HEIGHT, 0.0);
    float atmosphere_depth_at_dir = calculate_atmosphere_depth_at_dir(ws_camera_position
								      , ws_view_dir);
    float step_distance = atmosphere_depth_at_dir / float(SAMPLE_COUNT);

    vec3 total_mie = vec3(0.0);
    vec3 total_rayleigh = vec3(0.0);
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
	float distance = (float(i) * step_distance);
	vec3 ws_ray_position = distance * ws_view_dir;
	float ray_atmosphere_depth = calculate_atmosphere_depth_at_dir(ws_ray_position
								       , push_k.ws_light_direction.xyz);
	vec3 light_amount = absorb_light_from_sun_at_ray_position(ray_atmosphere_depth
								  , vec3(7)
								  , 0.5);
	
	total_rayleigh += absorb_light_from_sun_at_ray_position(distance
								, AIR_COLOR * light_amount
								, 0.9);
	total_mie += absorb_light_from_sun_at_ray_position(distance
							   , light_amount
							   , 0.01);
    }

    total_rayleigh = (total_rayleigh * pow(atmosphere_depth_at_dir, 0.5)) / float(SAMPLE_COUNT);
    total_mie = (total_mie * pow(atmosphere_depth_at_dir, 2.4)) / float(SAMPLE_COUNT);

    float spotlight = smoothstep(0.0, 15.0, phase(-difference_light_dir_view_dir, 0.9995))*0.4;

    out_color = (vec4(total_mie.xyzz) * spotlight + vec4(total_mie.xyzz) * mie_scattering_factor * 0.7 + vec4(total_rayleigh.xyzz) * rayleigh_scattering_factor);
}
