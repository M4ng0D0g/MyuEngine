#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform vec3 uLightPos;
uniform vec3 uViewPos;

void main() {
    vec3 norm     = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 color    = vec3(0.8) * (0.2 + 0.8 * diff);
    FragColor     = vec4(color, 1.0);
}
