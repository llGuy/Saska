#version 450

layout(location = 0) in VS_DATA
{
    vec2 uvs;
} fs_in;

layout(location = 0) out vec4 final_color;

layout(push_constant) uniform Push_K
{
    vec4 debug;
} pk;

layout(binding = 0, set = 0) uniform sampler2D previous;

void main(void)
{
    final_color = texture(previous, fs_in.uvs);

    //float vignette_factor = 1.0 / 4.0;
    //final_color.rgb *= pow(cos(length(fs_in.uvs * 2 - 1) * vignette_factor), 2);
}
