#version 450

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(location = 0) out vec4 out_final;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_position;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_sun;

layout(set = 1, binding = 0) uniform sampler2D sun_texture;
layout(set = 2, binding = 0) uniform samplerCube atmosphere;

layout(push_constant) uniform push_constant_t
{
    mat4 model;
    vec3 ws_light_direction;
} push_k;

void main(void)
{
    out_sun = texture(sun_texture, fs_in.uvs) * texture(atmosphere, push_k.ws_light_direction);
}
