#version 330 core

uniform sampler2D sf_texture;
uniform float blur_radius;

in vec4 v_color;
in vec2 v_texCoords;

out vec4 fragColor;

void main()
{
    vec2 offx = vec2(blur_radius, 0.0);
    vec2 offy = vec2(0.0, blur_radius);

    vec4 pixel = texture(sf_texture, v_texCoords)               * 4.0 +
                 texture(sf_texture, v_texCoords - offx)        * 2.0 +
                 texture(sf_texture, v_texCoords + offx)        * 2.0 +
                 texture(sf_texture, v_texCoords - offy)        * 2.0 +
                 texture(sf_texture, v_texCoords + offy)        * 2.0 +
                 texture(sf_texture, v_texCoords - offx - offy) * 1.0 +
                 texture(sf_texture, v_texCoords - offx + offy) * 1.0 +
                 texture(sf_texture, v_texCoords + offx - offy) * 1.0 +
                 texture(sf_texture, v_texCoords + offx + offy) * 1.0;

    fragColor = v_color * (pixel / 16.0);
}
