#version 330 core

layout(location = 0) in vec2 sf_position;
layout(location = 1) in vec4 sf_color;
layout(location = 2) in vec2 sf_texCoords;

uniform mat4 sf_projection;
uniform mat4 sf_model;
uniform mat4 sf_textureMatrix;

uniform vec2 storm_position;
uniform float storm_total_radius;
uniform float storm_inner_radius;

out vec4 v_color;
out vec2 v_texCoords;

void main()
{
    vec4 vertex = sf_model * vec4(sf_position, 0.0, 1.0);
    vec2 offset = vertex.xy - storm_position;
    float len = length(offset);
    if (len < storm_total_radius)
    {
        float push_distance = storm_inner_radius + len / storm_total_radius * (storm_total_radius - storm_inner_radius);
        vertex.xy = storm_position + normalize(offset) * push_distance;
    }

    gl_Position = sf_projection * vertex;
    vec4 tc = sf_textureMatrix * vec4(sf_texCoords, 0.0, 1.0);
    v_texCoords = tc.xy;
    v_color = sf_color;
}
