#version 330 core

layout(location = 0) in vec2 sf_position;
layout(location = 1) in vec4 sf_color;
layout(location = 2) in vec2 sf_texCoords;

uniform mat4 sf_projection;
uniform mat4 sf_model;

out vec4 v_color;
out vec3 v_normal;

void main()
{
    gl_Position = sf_projection * sf_model * vec4(sf_position, 0.0, 1.0);
    v_color = sf_color;
    v_normal = vec3(sf_texCoords, 1.0);
}
