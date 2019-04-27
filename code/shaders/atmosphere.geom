#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 24) out;

layout(location = 0) out vec3 out_cube_face_direction;
layout(location = 1) out int out_layer;

/* 0: GL_TEXTURE_CUBE_MAP_POSITIVE_X
   0: GL_TEXTURE_CUBE_MAP_NEGATIVE_X
   0: GL_TEXTURE_CUBE_MAP_POSITIVE_Y
   0: GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
   0: GL_TEXTURE_CUBE_MAP_POSITIVE_Z
   0: GL_TEXTURE_CUBE_MAP_NEGATIVE_Z */

const vec3 PREMADE_DIRECTIONS[] = vec3[] (vec3(+1, 0, 0),
					  vec3(-1, 0, 0),
					  vec3(0.000000001, +1, 0.000000001),
					  vec3(0.000000001, -1, 0.000000001),
					  vec3(0, 0, -1),
					  vec3(0, 0, +1));

void
main(void)
{
    for (int face_idx = 0; face_idx < 6; ++face_idx)
    {
	for (int vertex_idx = 0; vertex_idx < 4; ++vertex_idx)
	{
	    if (face_idx == 2 || face_idx == 3)
	    {
		vec2 nd = vec2((vertex_idx << 1) & 2, vertex_idx & 2);
		gl_Position = vec4(nd * (-2.0f) + 1.0f, 0.0f, 1.0f);
	    }
	    else
	    {
		gl_Position = vec4(vec2(1.0 - float((vertex_idx << 1) & 2), 1.0 - (vertex_idx & 2)) * 2.0f - 1.0f, 0.0f, 1.0f);
	    }
	    gl_Layer = face_idx;
	    out_cube_face_direction = PREMADE_DIRECTIONS[face_idx];
	    EmitVertex();
	}
	EndPrimitive();
    }
}
