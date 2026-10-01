#version 450 core

in vec3 vTexCoord;
in float vShade;
flat in uint vCutout;

uniform sampler2DArray uBlocks;

out vec4 fragColor;

void main()
{
    vec4 color = texture(uBlocks, vTexCoord);
    if (vCutout != 0u && color.a < 0.5) {
        discard;
    }
    fragColor = vec4(color.rgb * vShade, 1.0);
}
