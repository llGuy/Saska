#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 24) out;

layout(location = 0) out vec2 normal_xy;
layout(location = 1) out flat int face_index;

/* 0: GL_TEXTURE_CUBE_MAP_POSITIVE_X
   1: GL_TEXTURE_CUBE_MAP_NEGATIVE_X
   2: GL_TEXTURE_CUBE_MAP_POSITIVE_Y
   3: GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
   4: GL_TEXTURE_CUBE_MAP_POSITIVE_Z
   5: GL_TEXTURE_CUBE_MAP_NEGATIVE_Z */

const vec3 PREMADE_DIRECTIONS[] = vec3[] (vec3(1, 0, 0),
					  vec3(-1, 0, 0),
					  vec3(0.000000001, +1, 0.00000000),
					  vec3(0.000000001, -1, 0.00000000),
					  vec3(-1, 0, 0),
					  vec3(+1, 0, 0));

const vec2 NORMAL_XY[] = vec2[] ( vec2(-1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1) );

void main(void)
{
    for (int face_idx = 0; face_idx < 6; ++face_idx)
    {
	for (int vertex_idx = 0; vertex_idx < 4; ++vertex_idx)
	{
	    //gl_Position = vec4(vec2(1.0 - float((vertex_idx << 1) & 2), 1.0 - (vertex_idx & 2)) * 2.0f - 1.0f, 0.0f, 1.0f);
            gl_Position = vec4(NORMAL_XY[vertex_idx], 0, 1);
	    gl_Layer = face_idx;
            normal_xy = NORMAL_XY[vertex_idx];
            face_index = face_idx;
            
	    EmitVertex();
	}
	EndPrimitive();
    }
}
