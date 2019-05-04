#version 450

layout(location = 0) in vec2 in_uvs;

layout(location = 0) out vec4 out_color;

const uint G_BUFFER_ALBEDO	= 0;
const uint G_BUFFER_POSITION	= 1;
const uint G_BUFFER_NORMAL	= 2;
const uint G_BUFFER_TOTAL	= 4;

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput g_buffer_albedo;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput g_buffer_position;
layout(input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput g_buffer_normal;

layout(set = 1, binding = 0) uniform samplerCube cubemap_sampler;

layout(push_constant) uniform Push_K
{
    vec4 light_direction;
} push_k;

void
main(void)
{
    vec3 albedo_color = subpassLoad(g_buffer_albedo).rgb;
    vec3 ws_position = subpassLoad(g_buffer_position).rgb;
    vec3 ws_normal = subpassLoad(g_buffer_normal).rgb;

    out_color = vec4(albedo_color, 1.0);

    if (ws_normal.x > -10.0 && ws_normal.y > -10.0 && ws_normal.z > -10.0)
    {
	// calculate lighting (is not on the atmosphere)
	vec4 atmosphere_color = texture(cubemap_sampler, ws_normal);
	float alpha = clamp(dot(-vec3(push_k.light_direction.xyz), ws_normal), 0, 1);
	out_color = vec4(alpha * 0.8) * vec4(0.7, 0.6, 0.5, 1.0) + vec4(albedo_color, 1.0);
    }
}
