#version 450

layout(location = 0) in vec2 in_uvs;

layout(location = 0) out vec4 out_color;

const uint G_BUFFER_ALBEDO	= 0;
const uint G_BUFFER_POSITION	= 1;
const uint G_BUFFER_NORMAL	= 2;
const uint G_BUFFER_TOTAL	= 4;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput g_buffer_albedo;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput g_buffer_position;

void
main(void)
{
    vec3 albedo_color = subpassLoad(g_buffer_albedo).rgb;
    vec3 ws_position = subpassLoad(g_buffer_position).rgb;

    out_color = vec4(albedo_color, 1.0);
}
