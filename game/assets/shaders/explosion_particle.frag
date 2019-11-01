#version 450

layout(location = 0) in VS_DATA
{
    mat4 projection_matrix;
    vec3 projected_coords;
    vec3 vs_position;
    bool dead;
    vec2 before_uvs;
    vec2 after_uvs;
    float lerp;
    float width;
    float height;
} fs_in;

layout(location = 0) out vec4 final_color;

layout(set = 1, binding = 0) uniform sampler2D explosion_texture;
layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput g_buffer_position;

void main(void)
{
    if (fs_in.dead) discard;

    vec4 vs_position = subpassLoad(g_buffer_position);
    //    vec4 projected_position = fs_in.projection_matrix * vs_position;
    //    projected_position /= projected_position.w;

    float max = 3.0;
    float difference = clamp(fs_in.vs_position.z - vs_position.z, 0, max) / max;

    
    
    vec4 before = texture(explosion_texture, fs_in.before_uvs);
    vec4 after = texture(explosion_texture, fs_in.after_uvs);

    final_color = mix(before, after, fs_in.lerp);

    final_color.a *= difference;
}
