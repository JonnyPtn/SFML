#version 450

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 model;
    mat4 textureMatrix;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoords;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoords;

void main() {
    gl_Position = pc.projection * pc.model * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
    vec4 tc = pc.textureMatrix * vec4(inTexCoords, 0.0, 1.0);
    fragTexCoords = tc.xy;
}
