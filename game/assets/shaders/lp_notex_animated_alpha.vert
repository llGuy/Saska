#version 450

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view;
    mat4 shadow_proj;

    vec4 debug_vector;

    vec4 light_direction;
    mat4 inverse_view;
    vec4 view_direction;
} ubo;

layout(set = 5, binding = 0) uniform Joint_Transforms
{
    // TODO: Don't hardcode number of transforms
    mat4 transforms[13];
} joints;

// Animation UBO with the joint transforms

layout(push_constant) uniform Push_Constants
{
    mat4 model;
    vec4 color;

    float roughness;
    float metalness;
} push_k;

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;
layout(location = 2) in vec3 joint_weights;
layout(location = 3) in ivec3 affected_joint_ids;

layout(location = 0) out VS_DATA
{
    vec3 final;
    vec3 position;
    vec3 normal;
    vec4 shadow_coord;
    
} vs_out;

#define MAX_WEIGHTS 3

void main(void)
{
    mat4 identity = mat4(
                         1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1);

    vec4 accumulated_local = vec4(0);
    vec4 accumulated_normal = vec4(0);

    for (int i = 0; i < MAX_WEIGHTS; ++i)
    {
        vec4 original_pos = vec4(vertex_position, 1.0f);

        mat4 joint = joints.transforms[affected_joint_ids[i]];
		
        vec4 pose_position = joint * original_pos;

        accumulated_local += pose_position * joint_weights[i];

        vec4 world_normal = joint * vec4(vertex_normal, 0.0f);
        accumulated_normal += world_normal * joint_weights[i];
    }
	
    vec3 ws_position = vec3(push_k.model * ( accumulated_local));
    vec4 vs_position = ubo.view * vec4(ws_position, 1.0);

    vec4 vs_normal = ubo.view * accumulated_normal;
    
    gl_Position = ubo.proj * vs_position;
    vs_out.final = vec3(push_k.color);

    vs_out.position = vs_position.xyz;
    vs_out.normal = vs_normal.xyz;

    vs_out.shadow_coord = ubo.shadow_proj * ubo.shadow_view * vec4(ws_position, 1.0);
}
