#version 330 core

uniform sampler2D sf_texture;

in vec2 tex_coord;

out vec4 fragColor;

void main()
{
    fragColor = texture(sf_texture, tex_coord);
}
