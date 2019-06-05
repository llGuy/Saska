#version 450

layout(location = 0) out vec4 final_color;

void
main(void)
{
    // later render to appropriate PSSM
final_color = vec4(1.0, 0.0, 0.0, 1.0);
}
