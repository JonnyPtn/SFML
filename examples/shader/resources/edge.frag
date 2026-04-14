#version 330 core

uniform sampler2D sf_texture;
uniform float edge_threshold;

in vec4 v_color;
in vec2 v_texCoords;

out vec4 fragColor;

void main()
{
    const float offset = 1.0 / 512.0;
    vec2 offx = vec2(offset, 0.0);
    vec2 offy = vec2(0.0, offset);

    vec4 hEdge = texture(sf_texture, v_texCoords - offy)        * -2.0 +
                 texture(sf_texture, v_texCoords + offy)        *  2.0 +
                 texture(sf_texture, v_texCoords - offx - offy) * -1.0 +
                 texture(sf_texture, v_texCoords - offx + offy) *  1.0 +
                 texture(sf_texture, v_texCoords + offx - offy) * -1.0 +
                 texture(sf_texture, v_texCoords + offx + offy) *  1.0;

    vec4 vEdge = texture(sf_texture, v_texCoords - offx)        *  2.0 +
                 texture(sf_texture, v_texCoords + offx)        * -2.0 +
                 texture(sf_texture, v_texCoords - offx - offy) *  1.0 +
                 texture(sf_texture, v_texCoords - offx + offy) * -1.0 +
                 texture(sf_texture, v_texCoords + offx - offy) *  1.0 +
                 texture(sf_texture, v_texCoords + offx + offy) * -1.0;

    vec3 result = sqrt(hEdge.rgb * hEdge.rgb + vEdge.rgb * vEdge.rgb);
    float edge = length(result);
    vec4 pixel = v_color * texture(sf_texture, v_texCoords);
    if (edge > (edge_threshold * 8.0))
        pixel.rgb = vec3(0.0, 0.0, 0.0);
    else
        pixel.a = edge_threshold;
    fragColor = pixel;
}
