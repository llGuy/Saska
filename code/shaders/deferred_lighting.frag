#version 450

layout(location = 0) in vec2 in_uvs;

layout(location = 0) out vec4 out_color;

const uint G_BUFFER_ALBEDO	= 0;
const uint G_BUFFER_POSITION	= 1;
const uint G_BUFFER_NORMAL	= 2;
const uint G_BUFFER_TOTAL	= 4;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput g_buffer_albedo;
//layout(input_attachment_index = 1, binding = 1) uniform subpassInput g_buffer_position;
/*layout(input_attachment_index = 2, binding = 2) uniform subpassInput g_buffer_normal;*/

void
main(void)
{
    //   out_color = vec4(0.5f);
    //out_color = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    out_color = vec4(subpassLoad(g_buffer_albedo).rgb, 1.0);
    //    out_color = subpassLoad(g_buffer_final);
}
