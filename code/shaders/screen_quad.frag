#version 450

layout(location = 0) out vec4 final_color;

layout(location = 0) in vec2 in_uvs;

layout(set = 0, binding = 0) uniform sampler2D tex;

float linearize_depth(float depth)
{
    const float NEAR = 0.1;
    const float FAR = 100000.0f;

    float linear = NEAR * FAR / (FAR + depth * (NEAR - FAR));
    return(linear);
}

void
main(void)
{
    float d = texture(tex, in_uvs).r;

    final_color = vec4(d);
//    final_color = vec4(1 - d) * 10;
}
