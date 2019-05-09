#version 450

layout(binding = 0, location = 0) in vec2 ms_xz;
layout(binding = 1, location = 1) in float ms_y;

layout(location = 0) out vec3 frag_final;
layout(location = 1) out vec2 frag_uvs;
layout(location = 2) out vec3 frag_position;
layout(location = 3) out vec3 frag_normal;

layout(binding = 0) uniform Uniform_Buffer_Object
{
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform Push_Constants
{
    mat4 model;
} push_k;

void
main(void)
{
    // ---- create the 3D mesh position ----
    vec3 ms_position = vec3(ms_xz.x, ms_y, ms_xz.y);
    vec3 ws_position = push_k.model * vec4(ms_position, 1.0);

    gl_Position = ubo.proj * ubo.view * * vec4(ws_position, 1.0);

    frag_position = ws_position;
    frag_normal = vec3(0);
    frag_uvs = vec2(0);
    frag_final = vec3(0);
}
