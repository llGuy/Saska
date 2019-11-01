#version 450

layout(location = 0) in VS_DATA
{
    bool dead;
    vec2 before_uvs;
    vec2 after_uvs;
    float lerp;
    float width;
    float height;
} fs_in;

layout(location = 0) out vec4 final_color;

layout(set = 1, binding = 0) uniform sampler2D explosion_texture;

void main(void)
{
    if (fs_in.dead) discard;
    
    vec4 before = texture(explosion_texture, fs_in.before_uvs);
    vec4 after = texture(explosion_texture, fs_in.after_uvs);

    final_color = mix(before, after, fs_in.lerp);
}
