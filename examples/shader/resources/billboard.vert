#version 330 core

layout(location = 0) in vec2 sf_position;

uniform mat4 sf_projection;
uniform mat4 sf_model;

void main()
{
    gl_Position = sf_projection * sf_model * vec4(sf_position, 0.0, 1.0);
}
