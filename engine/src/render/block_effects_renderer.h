#pragma once

#include "client/block_particles.h"
#include "data/block_textures.h"
#include "render/shader.h"
#include "world/coordinates.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace aurora::client {
class Camera;
}

namespace aurora::render {

// What to draw over the chunks this frame. Each part is optional; the app decides when (selection lines and cracks
// only while the player takes input, cracks only while the attack is held).
struct BlockEffectsFrame {
    std::optional<world::BlockPos> outline; // The selected block: its 12 edges.
    struct Crack {
        world::BlockPos cell;
        std::uint32_t stage = 0; // 0..9
    };
    std::optional<Crack> crack;
    std::span<const client::BlockParticles::Instance> particles;
    // The effects check tool's negative control only: draw over everything, as if depth testing were off.
    bool ignoreDepth = false;
};

struct BlockEffectsStats {
    std::size_t drawCalls = 0; // In the last draw.
    std::size_t particles = 0;
};

// Block selection lines, crack overlays and fragments, drawn after the chunks and before the UI. Main thread only
// (GL objects).
//
// - Lines: the selected cell's 12 edges, just outside the cell, 1 pixel wide (what GL core guarantees), depth tested
//   so walls in front hide them.
// - Cracks: the crack stage texture on the cell's 6 faces, blended with straight alpha (transparent texels leave the
//   block as it is), depth tested without writing depth, pulled forward with a polygon offset; back faces culled.
// - Fragments: squares facing the camera in each fragment's colour, depth tested without writing depth.
// - Every GL state it changes is given back afterwards (GlStateSnapshot), so the chunk and UI passes are untouched.
// Positions are made camera-relative in double first (client::relativeTo).
class BlockEffectsRenderer {
public:
    static constexpr std::uint32_t kCrackTextureUnit = 1;

    BlockEffectsRenderer() = default;
    ~BlockEffectsRenderer();

    BlockEffectsRenderer(const BlockEffectsRenderer&) = delete;
    BlockEffectsRenderer& operator=(const BlockEffectsRenderer&) = delete;

    // Loads effects.vert/.frag and particles.vert/.frag from `shaderFolder` and uploads the crack stages (each
    // data::kBlockTextureSize square). Requires a GL context.
    bool init(const std::filesystem::path& shaderFolder, std::span<const data::RgbaImage> crackStages,
              std::string& error);
    // Frees every GL object; call before the context goes away. Safe to repeat.
    void shutdown();

    void draw(const client::Camera& camera, float aspect, float farPlane, const BlockEffectsFrame& frame);
    BlockEffectsStats stats() const { return m_stats; }

private:
    ShaderProgram m_effects;
    ShaderProgram m_particles;
    std::int32_t m_effectsViewProjection = -1;
    std::int32_t m_effectsTextured = -1;
    std::int32_t m_effectsColor = -1;
    std::int32_t m_particlesViewProjection = -1;
    std::int32_t m_particlesRight = -1;
    std::int32_t m_particlesUp = -1;
    std::uint32_t m_crackTexture = 0;
    std::uint32_t m_crackStages = 0;
    std::uint32_t m_effectsArray = 0;
    std::uint32_t m_effectsBuffer = 0;
    std::uint32_t m_particleArray = 0;
    std::uint32_t m_cornerBuffer = 0;
    std::uint32_t m_instanceBuffer = 0;
    std::size_t m_instanceCapacity = 0;
    BlockEffectsStats m_stats;
};

} // namespace aurora::render
