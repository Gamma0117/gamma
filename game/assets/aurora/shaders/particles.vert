#version 450 core

// Block fragments: one square per instance, facing the camera.
layout(location = 0) in vec2 aCorner;    // -0.5..0.5 on the square
layout(location = 1) in vec3 aCenter;    // Camera-relative centre (per instance)
layout(location = 2) in vec4 aColorSize; // rgb 0..1 and the edge length in blocks (per instance)

uniform mat4 uViewProjection;
uniform vec3 uCameraRight; // World-space screen right and up of the camera.
uniform vec3 uCameraUp;

out vec3 vColor;

void main()
{
    vColor = aColorSize.rgb;
    vec3 position = aCenter + (uCameraRight * aCorner.x + uCameraUp * aCorner.y) * aColorSize.a;
    gl_Position = uViewProjection * vec4(position, 1.0);
}
