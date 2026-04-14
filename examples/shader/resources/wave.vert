#version 330 core

layout(location = 0) in vec2 sf_position;
layout(location = 1) in vec4 sf_color;
layout(location = 2) in vec2 sf_texCoords;

uniform mat4 sf_projection;
uniform mat4 sf_model;
uniform mat4 sf_textureMatrix;

uniform float wave_phase;
uniform vec2 wave_amplitude;

out vec4 v_color;
out vec2 v_texCoords;

void main()
{
    vec2 vertex = sf_position;
    vertex.x += cos(sf_position.y * 0.02 + wave_phase * 3.8) * wave_amplitude.x
              + sin(sf_position.y * 0.02 + wave_phase * 6.3) * wave_amplitude.x * 0.3;
    vertex.y += sin(sf_position.x * 0.02 + wave_phase * 2.4) * wave_amplitude.y
              + cos(sf_position.x * 0.02 + wave_phase * 5.2) * wave_amplitude.y * 0.3;

    gl_Position = sf_projection * sf_model * vec4(vertex, 0.0, 1.0);
    vec4 tc = sf_textureMatrix * vec4(sf_texCoords, 0.0, 1.0);
    v_texCoords = tc.xy;
    v_color = sf_color;
}
