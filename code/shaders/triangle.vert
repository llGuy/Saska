#version 450

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

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_color;
layout(location = 2) in vec2 uvs;

layout(location = 0) out vec3 frag_final;
layout(location = 1) out vec2 frag_uvs;
layout(location = 2) out vec3 frag_position;
layout(location = 3) out vec3 frag_normal;

void
main(void)
{
    vec4 ws_position = push_k.model * vec4(vertex_position, 1.0);
    
    gl_Position = ubo.proj * ubo.view * ws_position;
    frag_final = vertex_color;
    frag_uvs = uvs;

    frag_position = ws_position.xyz;

    frag_normal = vec3(push_k.model * vec4(normalize(vertex_position), 0.0));
    // for the moment, just using this to test lighting
}
