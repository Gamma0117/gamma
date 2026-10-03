#version 450 core

// Block selection lines and crack overlays (BlockEffectsRenderer), positions relative to the camera.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aTexCoord; // u, v (from the texture's top edge), crack stage (layer)

uniform mat4 uViewProjection; // Projection * view rotation; positions are already camera-relative.

out vec3 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
