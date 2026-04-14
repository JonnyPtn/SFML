
uniform float lightFactor;

in vec4 v_color;
in vec3 v_normal;

out vec4 fragColor;

void main()
{
    vec3 lightPosition = vec3(-1.0, 1.0, 1.0);
    vec3 eyePosition = vec3(0.0, 0.0, 1.0);
    vec3 halfVector = normalize(lightPosition + eyePosition);
    float intensity = lightFactor + (1.0 - lightFactor) * dot(normalize(v_normal), normalize(halfVector));
    fragColor = v_color * vec4(intensity, intensity, intensity, 1.0);
}
