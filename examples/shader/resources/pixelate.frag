#version 330 core

uniform sampler2D sf_texture;
uniform float pixel_threshold;

in vec4 v_color;
in vec2 v_texCoords;

out vec4 fragColor;

void main()
{
    float factor = 1.0 / (pixel_threshold + 0.001);
    vec2 pos = floor(v_texCoords * factor + 0.5) / factor;
    fragColor = texture(sf_texture, pos) * v_color;
}
