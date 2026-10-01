#version 450 core

// Chunk section vertex. The packing matches engine/src/render/mesh_vertex.h; change both together.
//   word 0: x 0-4, y 5-9, z 10-14 (0..16 in the section), u 15-19, v 20-24 (blocks, v from the texture's top
//           edge), ao 25-26 (corner brightness, 3 = open), face 27-29, cutout 30
//   word 1: texture array layer 0-15
layout(location = 0) in uvec2 aPacked;

uniform mat4 uViewProjection; // Projection * view rotation; the view has no translation.
uniform vec3 uSectionOffset;  // Section origin minus camera position, computed in double on the CPU.

out vec3 vTexCoord; // u, v, layer
out float vShade;
flat out uint vCutout;

// Until lighting exists (P0-9): darker corners for AO and a fixed brightness per face direction, so shapes read.
const float kAoBrightness[4] = float[](0.45, 0.62, 0.8, 1.0);
const float kFaceBrightness[6] = float[](0.5, 1.0, 0.8, 0.8, 0.65, 0.65); // down, up, north, south, west, east

void main()
{
    uint word0 = aPacked.x;
    vec3 position = vec3(float(word0 & 31u), float((word0 >> 5) & 31u), float((word0 >> 10) & 31u));
    float u = float((word0 >> 15) & 31u);
    float v = float((word0 >> 20) & 31u);
    uint ao = (word0 >> 25) & 3u;
    uint face = (word0 >> 27) & 7u;
    vCutout = (word0 >> 30) & 1u;
    vTexCoord = vec3(u, v, float(aPacked.y & 0xffffu));
    vShade = kAoBrightness[ao] * kFaceBrightness[face];
    gl_Position = uViewProjection * vec4(uSectionOffset + position, 1.0);
}
