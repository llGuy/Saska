#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_normal;
layout(location = 2) in vec3 joint_weights;
layout(location = 3) in ivec3 affected_joint_ids;

layout(set = 0, binding = 0) uniform Uniform_Buffer_Object
{
    mat4 view;
    mat4 proj;

    mat4 shadow_view[4];
    mat4 shadow_proj[4];
    
    vec4 debug_vector;
} ubo;

layout(set = 1, binding = 0) uniform Joint_Transforms
{
    // TODO: Don't hardcode number of transforms
    mat4 transforms[13];
} joints;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
} push_k;

#define MAX_WEIGHTS 3

void
main(void)
{
    mat4 identity = mat4(
                         1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1);

    vec4 accumulated_local = vec4(0);

    for (int i = 0; i < MAX_WEIGHTS; ++i)
    {
        vec4 original_pos = vec4(vertex_position, 1.0f);

        mat4 joint = joints.transforms[affected_joint_ids[i]];
		
        vec4 pose_position = joint * original_pos;

        accumulated_local += pose_position * joint_weights[i];
    }
	
    vec3 ws_position = vec3(push_k.model * ( accumulated_local));
    
    gl_Position = ubo.shadow_proj[0] * ubo.shadow_view[0] * vec4(ws_position, 1.0);
}
