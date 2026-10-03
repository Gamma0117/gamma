#version 450 core

in vec3 vTexCoord;

uniform int uTextured;           // 0: the selection lines in uColor; 1: a crack stage from uCracks
uniform vec4 uColor;
uniform sampler2DArray uCracks;

out vec4 fragColor;

void main()
{
    if (uTextured == 0) {
        fragColor = uColor;
        return;
    }
    // Straight alpha: blended with (SRC_ALPHA, ONE_MINUS_SRC_ALPHA); fully transparent texels leave the block as is.
    vec4 crack = texture(uCracks, vTexCoord);
    if (crack.a <= 0.0) {
        discard;
    }
    fragColor = crack;
}
