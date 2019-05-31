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


layout(location = 0) out VS_DATA
{

    vec3 final;
    vec2 uvs;
    vec3 position;
    vec3 normal;
    
} vs_out;

void
main(void)
{
    vec4 ws_position = push_k.model * vec4(vertex_position, 1.0);
    
    gl_Position = ubo.proj * ubo.view * ws_position;
    vs_out.final = vertex_color;
    vs_out.uvs = uvs;

    vs_out.position = ws_position.xyz;

    vs_out.normal = vec3(push_k.model * vec4(normalize(vertex_position), 0.0));
    // for the moment, just using this to test lighting
}
