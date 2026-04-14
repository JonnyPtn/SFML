
uniform float blink_alpha;

in vec4 v_color;

out vec4 fragColor;

void main()
{
    vec4 pixel = v_color;
    pixel.a = blink_alpha;
    fragColor = pixel;
}
