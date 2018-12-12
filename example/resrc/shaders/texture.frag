#version 150

uniform sampler2D uImage;

in vec2 vTexCoord;

out vec4 FragColor;

void main() {
    FragColor = texture(uImage, vTexCoord);
}