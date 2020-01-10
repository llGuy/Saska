#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(push_constant) uniform push_constant_t
{
    mat4 matrix;
    float roughness;
    float layer;
} push_k;

const vec3 PREMADE_DIRECTIONS[] = vec3[] (vec3(0, 0, -1),
					  vec3(0, 0, +1),
					  vec3(0.000000001, +1, 0.00000000),
					  vec3(0.000000001, -1, 0.00000000),
					  vec3(-1, 0, 0),
					  vec3(+1, 0, 0));

void main(void)
{
    
}
