#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoords;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(texSampler, fragTexCoords);
}
