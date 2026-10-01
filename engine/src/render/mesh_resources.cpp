#include "render/mesh_resources.h"

#include "render/mesh_vertex.h"

namespace aurora::render {

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
        for (std::size_t face = 0; face < data::kBlockFaceCount; ++face) {
            const auto found = layers.layerOf.find(block.faceTextures[face].str());
            look.layers[face] = found == layers.layerOf.end() ? kMissingTextureLayer : found->second;
        }
    }
    return resources;
}

} // namespace aurora::render
