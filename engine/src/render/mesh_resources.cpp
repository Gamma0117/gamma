#include "render/mesh_resources.h"

#include "render/face_frames.h"
#include "render/mesh_vertex.h"

#include <cassert>
#include <optional>
#include <string_view>

namespace aurora::render {

namespace {

using Vector = std::array<std::int32_t, 3>;

// The model turned into the axis state: axis 0 x: (x, y, z) -> (y, -x, z); 1 y: unchanged; 2 z: (x, y, z) ->
// (x, -z, y).
Vector turn(int axis, const Vector& v)
{
    switch (axis) {
    case 0:
        return {v[1], -v[0], v[2]};
    case 2:
        return {v[0], -v[2], v[1]};
    default:
        return v;
    }
}

Vector negate(const Vector& v)
{
    return {-v[0], -v[1], -v[2]};
}

// The axis index (0 x, 1 y, 2 z) of `state`'s block's `axis` property, if it has one with one of those values.
std::optional<int> axisOf(const data::BlockRegistry& registry, data::BlockStateId state)
{
    const data::BlockDefinition& block = registry.blockOf(state);
    for (std::size_t index = 0; index < block.properties.size(); ++index) {
        const data::BlockProperty& property = block.properties[index];
        if (property.name != "axis") {
            continue;
        }
        const std::string_view value = property.values[registry.propertyValue(state, index)];
        if (value == "x") {
            return 0;
        }
        if (value == "y") {
            return 1;
        }
        if (value == "z") {
            return 2;
        }
    }
    return std::nullopt;
}

} // namespace

std::array<FaceSource, data::kBlockFaceCount> axisFaceSources(int axis)
{
    std::array<FaceSource, data::kBlockFaceCount> sources{};
    for (std::size_t source = 0; source < data::kBlockFaceCount; ++source) {
        const FaceFrame& model = kFaceFrames[source];
        const Vector normal = turn(axis, model.normal);
        const Vector right = turn(axis, model.right); // Where the texture's right ends up.
        for (std::size_t face = 0; face < data::kBlockFaceCount; ++face) {
            const FaceFrame& target = kFaceFrames[face];
            if (target.normal != normal) {
                continue;
            }
            std::uint8_t rotation = 0;
            if (right == target.up) {
                rotation = 1;
            } else if (right == negate(target.right)) {
                rotation = 2;
            } else if (right == negate(target.up)) {
                rotation = 3;
            } else {
                assert(right == target.right);
            }
            sources[face] = {static_cast<data::BlockFace>(source), rotation};
        }
    }
    return sources;
}

std::optional<TextureLayers> assignTextureLayers(std::span<const std::string> textureIds)
{
    if (textureIds.size() + 1 > kMaxTextureLayers) {
        return std::nullopt;
    }
    TextureLayers layers;
    for (const std::string& id : textureIds) {
        layers.layerOf.emplace(id, static_cast<std::uint16_t>(layers.layerCount));
        ++layers.layerCount;
    }
    return layers;
}

std::shared_ptr<const MeshResources> buildMeshResources(const data::BlockRegistry& registry,
                                                        const TextureLayers& layers)
{
    auto resources = std::make_shared<MeshResources>();
    resources->states.resize(registry.stateCount());
    for (std::uint32_t state = 0; state < registry.stateCount(); ++state) {
        const data::BlockDefinition& block = registry.blockOf(static_cast<data::BlockStateId>(state));
        StateLook& look = resources->states[state];
        switch (block.render) {
        case data::RenderLayer::Invisible:
            look.material = FaceMaterial::None;
            break;
        case data::RenderLayer::Opaque:
            look.material = FaceMaterial::Opaque;
            look.occludes = true;
            break;
        case data::RenderLayer::Cutout:
        case data::RenderLayer::Translucent: // Sorting and blending come with water (P0-10).
            look.material = FaceMaterial::Cutout;
            break;
        }
        const std::optional<int> axis = axisOf(registry, static_cast<data::BlockStateId>(state));
        const std::array<FaceSource, data::kBlockFaceCount> sources = axisFaceSources(axis.value_or(1));
        for (std::size_t face = 0; face < data::kBlockFaceCount; ++face) {
            const FaceSource& source = sources[face];
            const auto found = layers.layerOf.find(block.faceTextures[static_cast<std::size_t>(source.face)].str());
            look.layers[face] = found == layers.layerOf.end() ? kMissingTextureLayer : found->second;
            look.rotations[face] = source.rotation;
        }
    }
    return resources;
}

} // namespace aurora::render
