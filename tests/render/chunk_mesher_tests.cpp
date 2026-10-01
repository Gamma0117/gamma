#include "data/block_registry.h"
#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/mesh_vertex.h"

#include "mesh_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace aurora::render;
using aurora::test::quadsOf;
using aurora::test::TestNeighbourhood;
namespace state = aurora::test::state;

namespace {

using Quad = std::array<MeshVertex, 4>;

enum FaceIndex : std::uint8_t { kDown, kUp, kNorth, kSouth, kWest, kEast };

constexpr std::array<glm::ivec3, 6> kNormals{{{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}};

aurora::client::MeshData mesh(const TestNeighbourhood& blocks)
{
    static const auto resources = aurora::test::meshResourcesForTests();
    return meshSection(blocks.build(), *resources);
}

std::vector<Quad> quadsFacing(const std::vector<Quad>& quads, std::uint8_t face)
{
    std::vector<Quad> result;
    std::ranges::copy_if(quads, std::back_inserter(result), [face](const Quad& q) { return q[0].face == face; });
    return result;
}

glm::ivec3 positionOf(const MeshVertex& v)
{
    return {v.x, v.y, v.z};
}

// Extent of a quad along each axis.
glm::ivec3 sizeOf(const Quad& quad)
{
    glm::ivec3 low(99);
    glm::ivec3 high(-1);
    for (const MeshVertex& v : quad) {
        low = glm::min(low, positionOf(v));
        high = glm::max(high, positionOf(v));
    }
    return high - low;
}

const MeshVertex* vertexAt(const Quad& quad, glm::ivec3 position)
{
    for (const MeshVertex& v : quad) {
        if (positionOf(v) == position) {
            return &v;
        }
    }
    return nullptr;
}

// Every triangle is counter-clockwise seen from outside: its normal points the way the face looks.
bool windingFacesOutward(const aurora::client::MeshData& data)
{
    for (std::size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        std::array<MeshVertex, 3> v;
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t index = data.indices[i + k];
            v[k] = unpackVertex(data.vertexWords[index * 2], data.vertexWords[index * 2 + 1]);
        }
        const glm::vec3 edge1(positionOf(v[1]) - positionOf(v[0]));
        const glm::vec3 edge2(positionOf(v[2]) - positionOf(v[0]));
        if (glm::dot(glm::cross(edge1, edge2), glm::vec3(kNormals[v[0].face])) <= 0.0f) {
            return false;
        }
    }
    return true;
}

// The two triangles of quad `q` share two vertices: the diagonal. Their AO values.
std::array<std::uint8_t, 2> diagonalAo(const aurora::client::MeshData& data, std::size_t q)
{
    const std::uint32_t base = static_cast<std::uint32_t>(q * 4);
    std::array<std::uint32_t, 6> indices{};
    std::copy_n(data.indices.begin() + static_cast<std::ptrdiff_t>(q * 6), 6, indices.begin());
    std::vector<std::uint32_t> shared;
    for (std::size_t a = 0; a < 3; ++a) {
        if (std::find(indices.begin() + 3, indices.end(), indices[a]) != indices.end()) {
            shared.push_back(indices[a]);
        }
    }
    REQUIRE(shared.size() == 2);
    const auto ao = [&](std::uint32_t index) {
        REQUIRE(index >= base);
        return unpackVertex(data.vertexWords[index * 2], data.vertexWords[index * 2 + 1]).ao;
    };
    return {ao(shared[0]), ao(shared[1])};
}

} // namespace

TEST_CASE("Vertices pack and unpack at the edges of every field", "[render][mesh]")
{
    const MeshVertex top{16, 16, 16, 16, 16, 3, 5, true, 65535};
    const MeshVertex bottom{0, 0, 0, 0, 0, 0, 0, false, 0};
    for (const MeshVertex& vertex : {top, bottom}) {
        const auto words = packVertex(vertex);
        CHECK(unpackVertex(words[0], words[1]) == vertex);
        CHECK((words[0] >> 31) == 0u); // Reserved bits stay 0.
        CHECK((words[1] >> 16) == 0u);
    }
    for (std::uint8_t face = 0; face < 6; ++face) {
        for (std::uint8_t ao = 0; ao <= 3; ++ao) {
            const MeshVertex vertex{1, 2, 3, 4, 5, ao, face, false, 7};
            const auto words = packVertex(vertex);
            CHECK(unpackVertex(words[0], words[1]) == vertex);
        }
    }
}

TEST_CASE("A single block has six faces of one block each", "[render][mesh]")
{
    TestNeighbourhood blocks;
    blocks.set(5, 5, 5, state::kStone);
    const auto data = mesh(blocks);
    const std::vector<Quad> quads = quadsOf(data);
    REQUIRE(quads.size() == 6);
    CHECK(data.indices.size() == 36);
    for (std::uint8_t face = 0; face < 6; ++face) {
        const std::vector<Quad> facing = quadsFacing(quads, face);
        REQUIRE(facing.size() == 1);
        for (const MeshVertex& v : facing[0]) {
            CHECK(v.ao == 3);
            CHECK(v.layer == 1);
            CHECK_FALSE(v.cutout);
        }
    }
    const Quad up = quadsFacing(quads, kUp)[0];
    CHECK(vertexAt(up, {5, 6, 5}));
    CHECK(vertexAt(up, {6, 6, 6}));
    CHECK(windingFacesOutward(data));
}

TEST_CASE("Neighbouring faces merge into rectangles", "[render][mesh]")
{
    SECTION("Two blocks in a row")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kStone);
        blocks.set(6, 5, 5, state::kStone);
        const std::vector<Quad> quads = quadsOf(mesh(blocks));
        CHECK(quads.size() == 6);
        CHECK(sizeOf(quadsFacing(quads, kUp)[0]) == glm::ivec3(2, 0, 1));
    }
    SECTION("A full section in the open")
    {
        TestNeighbourhood blocks;
        blocks.fill(0, 5, 0, state::kStone);
        const auto data = mesh(blocks);
        const std::vector<Quad> quads = quadsOf(data);
        REQUIRE(quads.size() == 6);
        CHECK(sizeOf(quadsFacing(quads, kUp)[0]) == glm::ivec3(16, 0, 16));
        CHECK(sizeOf(quadsFacing(quads, kEast)[0]) == glm::ivec3(0, 16, 16));
        CHECK(windingFacesOutward(data));
    }
    SECTION("A full section buried in stone has no faces")
    {
        TestNeighbourhood blocks;
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                for (std::int32_t section = 4; section <= 6; ++section) {
                    blocks.fill(dx, section, dz, state::kStone);
                }
            }
        }
        CHECK(mesh(blocks).empty());

        // One air block in a wrapping section opens one face; one in a diagonal section opens none.
        TestNeighbourhood dug = std::move(blocks);
        dug.set(-16 + 15, 3, 3, state::kAir); // West neighbour chunk, next to the border.
        dug.set(31, 31, 31, state::kAir);      // Diagonal chunk above: only a corner touches.
        const std::vector<Quad> quads = quadsOf(mesh(dug));
        REQUIRE(quads.size() == 1);
        CHECK(quads[0][0].face == kWest);
    }
    SECTION("A buried section of glass still shows its faces")
    {
        TestNeighbourhood blocks;
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                for (std::int32_t section = 4; section <= 6; ++section) {
                    blocks.fill(dx, section, dz, state::kStone);
                }
            }
        }
        blocks.fill(0, 5, 0, state::kGlass); // Cutout hides nothing, so the faces between glass blocks remain.
        CHECK_FALSE(mesh(blocks).empty());
    }
}

TEST_CASE("A flat surface section is one top face", "[render][mesh]")
{
    TestNeighbourhood blocks;
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            blocks.fill(dx, 4, dz, state::kStone);
        }
    }
    for (std::int32_t x = -16; x < 32; ++x) {
        for (std::int32_t z = -16; z < 32; ++z) {
            for (std::int32_t y = 0; y < 16; ++y) {
                blocks.set(x, y, z, y == 15 ? state::kGrass : state::kStone);
            }
        }
    }
    const std::vector<Quad> quads = quadsOf(mesh(blocks));
    REQUIRE(quads.size() == 1);
    CHECK(quads[0][0].face == kUp);
    CHECK(quads[0][0].layer == 2); // Grass top.
    CHECK(sizeOf(quads[0]) == glm::ivec3(16, 0, 16));
    for (const MeshVertex& v : quads[0]) {
        CHECK(v.y == 16);
        CHECK(v.ao == 3);
    }
}

TEST_CASE("Faces take their block's texture layer and material", "[render][mesh]")
{
    SECTION("Grass uses top, bottom and side textures")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kGrass);
        const std::vector<Quad> quads = quadsOf(mesh(blocks));
        CHECK(quadsFacing(quads, kUp)[0][0].layer == 2);
        CHECK(quadsFacing(quads, kDown)[0][0].layer == 3);
        for (const std::uint8_t side : {kNorth, kSouth, kWest, kEast}) {
            CHECK(quadsFacing(quads, side)[0][0].layer == 4);
        }
    }
    SECTION("Different textures do not merge")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kStone);
        blocks.set(6, 5, 5, state::kGrass);
        CHECK(quadsFacing(quadsOf(mesh(blocks)), kUp).size() == 2);
    }
    SECTION("Opaque and cutout with the same texture do not merge and cutout does not hide faces")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kOpaque5);
        blocks.set(6, 5, 5, state::kGlass);
        const std::vector<Quad> quads = quadsOf(mesh(blocks));
        const std::vector<Quad> up = quadsFacing(quads, kUp);
        REQUIRE(up.size() == 2);
        CHECK(up[0][0].layer == 5);
        CHECK(up[1][0].layer == 5);
        CHECK(up[0][0].cutout != up[1][0].cutout);
        CHECK(quadsFacing(quads, kEast).size() == 2); // The opaque block's east face shows through the glass.
        CHECK(quadsFacing(quads, kWest).size() == 1); // The glass face against the opaque block is hidden.
    }
    SECTION("Invisible blocks have no faces and hide nothing")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kInvisible);
        CHECK(mesh(blocks).empty());
        blocks.set(6, 5, 5, state::kStone);
        CHECK(quadsOf(mesh(blocks)).size() == 6);
    }
    SECTION("The unknown block uses the missing texture")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kUnknown);
        CHECK(quadsOf(mesh(blocks))[0][0].layer == kMissingTextureLayer);
    }
}

TEST_CASE("The world's bottom has no faces and its top does", "[render][mesh]")
{
    TestNeighbourhood bottom(0);
    bottom.set(5, 0, 5, state::kStone);
    const std::vector<Quad> low = quadsOf(mesh(bottom));
    CHECK(quadsFacing(low, kDown).empty());
    CHECK(quadsFacing(low, kUp).size() == 1);

    TestNeighbourhood top(aurora::core::kSectionsPerChunk - 1);
    top.set(5, 15, 5, state::kStone);
    const std::vector<Quad> high = quadsOf(mesh(top));
    REQUIRE(quadsFacing(high, kUp).size() == 1);
    CHECK(quadsFacing(high, kUp)[0][0].y == 16);
}

TEST_CASE("The halo reaches across chunk and section borders", "[render][mesh]")
{
    SECTION("A block in the next chunk hides the face")
    {
        TestNeighbourhood blocks;
        blocks.set(15, 5, 8, state::kStone);
        blocks.set(16, 5, 8, state::kStone);
        CHECK(quadsFacing(quadsOf(mesh(blocks)), kEast).empty());
    }
    SECTION("A block in the section above hides the face")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 15, 5, state::kStone);
        blocks.set(5, 16, 5, state::kStone);
        CHECK(quadsFacing(quadsOf(mesh(blocks)), kUp).empty());
    }
    SECTION("A block in the diagonal chunk darkens one corner")
    {
        TestNeighbourhood blocks;
        blocks.set(15, 5, 15, state::kStone);
        blocks.set(16, 6, 16, state::kStone); // Chunk (+1, +1), one layer up.
        const Quad up = quadsFacing(quadsOf(mesh(blocks)), kUp)[0];
        const MeshVertex* corner = vertexAt(up, {16, 6, 16});
        REQUIRE(corner);
        CHECK(corner->ao == 2);
        int bright = 0;
        for (const MeshVertex& v : up) {
            bright += v.ao == 3 ? 1 : 0;
        }
        CHECK(bright == 3);
    }
}

TEST_CASE("Corner AO counts occluders and the diagonal runs through the darker corners", "[render][mesh]")
{
    // The top face of a block at (5, 5, 5); occluders go in the layer above, y 6.
    const auto upFace = [](std::initializer_list<glm::ivec3> occluders) {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kStone);
        for (const glm::ivec3& p : occluders) {
            blocks.set(p.x, p.y, p.z, state::kStone);
        }
        return mesh(blocks);
    };
    const auto upQuadIndex = [](const aurora::client::MeshData& data) {
        const std::vector<Quad> quads = quadsOf(data);
        for (std::size_t i = 0; i < quads.size(); ++i) {
            if (quads[i][0].face == kUp && quads[i][0].y == 6) {
                return i;
            }
        }
        FAIL("no top face");
        return std::size_t{0};
    };

    SECTION("Only the diagonal block: brightness 2")
    {
        const auto data = upFace({{4, 6, 6}});
        const Quad up = quadsOf(data)[upQuadIndex(data)];
        CHECK(vertexAt(up, {5, 6, 6})->ao == 2);
        CHECK(vertexAt(up, {6, 6, 5})->ao == 3);
    }
    SECTION("Both sides: brightness 0 whatever the diagonal")
    {
        const auto data = upFace({{4, 6, 5}, {5, 6, 6}});
        const Quad up = quadsOf(data)[upQuadIndex(data)];
        CHECK(vertexAt(up, {5, 6, 6})->ao == 0);
    }
    SECTION("One side: brightness 2 on both corners along it")
    {
        const auto data = upFace({{4, 6, 5}});
        const Quad up = quadsOf(data)[upQuadIndex(data)];
        CHECK(vertexAt(up, {5, 6, 5})->ao == 2);
        CHECK(vertexAt(up, {5, 6, 6})->ao == 2);
        CHECK(vertexAt(up, {6, 6, 6})->ao == 3);
    }
    // One dark corner at each end of the default diagonal and of the other one: the shared edge of the two
    // triangles always passes through the dark corner, and the winding stays outward.
    for (const glm::ivec3 occluder : {glm::ivec3{4, 6, 6}, glm::ivec3{6, 6, 6}, glm::ivec3{6, 6, 4},
                                      glm::ivec3{4, 6, 4}}) {
        INFO("occluder " << occluder.x << ", " << occluder.z);
        const auto data = upFace({occluder});
        const std::array<std::uint8_t, 2> diagonal = diagonalAo(data, upQuadIndex(data));
        CHECK(std::min(diagonal[0], diagonal[1]) == 2);
        CHECK(windingFacesOutward(data));
    }
}

TEST_CASE("Texture coordinates repeat per block and keep textures upright", "[render][mesh]")
{
    SECTION("One block")
    {
        TestNeighbourhood blocks;
        blocks.set(5, 5, 5, state::kStone);
        const std::vector<Quad> quads = quadsOf(mesh(blocks));
        for (const Quad& quad : quads) {
            for (const MeshVertex& v : quad) {
                CHECK(v.u <= 1);
                CHECK(v.v <= 1);
                if (v.face >= kNorth) {
                    CHECK(v.v == (v.y == 6 ? 0 : 1)); // Side faces: the texture's top edge at the block's top.
                }
            }
        }
        // North face, seen from the north: its left edge is the east side (x = 6).
        CHECK(vertexAt(quadsFacing(quads, kNorth)[0], {6, 6, 5})->u == 0);
        CHECK(vertexAt(quadsFacing(quads, kNorth)[0], {5, 6, 5})->u == 1);
        // East face, seen from the east: its left edge is the south side (z = 6).
        CHECK(vertexAt(quadsFacing(quads, kEast)[0], {6, 6, 6})->u == 0);
        // Top face: u along +x, the texture's top edge to the north (-z).
        CHECK(vertexAt(quadsFacing(quads, kUp)[0], {5, 6, 5})->u == 0);
        CHECK(vertexAt(quadsFacing(quads, kUp)[0], {5, 6, 5})->v == 0);
        CHECK(vertexAt(quadsFacing(quads, kUp)[0], {6, 6, 6})->u == 1);
        CHECK(vertexAt(quadsFacing(quads, kUp)[0], {6, 6, 6})->v == 1);
    }
    SECTION("A 16 x 16 face repeats the texture 16 times each way")
    {
        TestNeighbourhood blocks;
        blocks.fill(0, 5, 0, state::kStone);
        const Quad up = quadsFacing(quadsOf(mesh(blocks)), kUp)[0];
        CHECK(vertexAt(up, {0, 16, 0})->u == 0);
        CHECK(vertexAt(up, {16, 16, 16})->u == 16);
        CHECK(vertexAt(up, {16, 16, 16})->v == 16);
    }
}

TEST_CASE("The densest section needs 32-bit indices", "[render][mesh]")
{
    // Two cutout materials in a 3D checkerboard: nothing hides anything and nothing merges.
    TestNeighbourhood blocks;
    for (std::int32_t y = 0; y < 16; ++y) {
        for (std::int32_t z = 0; z < 16; ++z) {
            for (std::int32_t x = 0; x < 16; ++x) {
                blocks.set(x, y, z, (x + y + z) % 2 == 0 ? state::kGlass : state::kLeaves);
            }
        }
    }
    const auto data = mesh(blocks);
    CHECK(data.vertexCount() == 4096u * 6 * 4);
    CHECK(data.indices.size() == 4096u * 6 * 6);
    CHECK(std::ranges::all_of(data.indices, [&](std::uint32_t index) { return index < data.vertexCount(); }));
    CHECK(*std::ranges::max_element(data.indices) > 65535u);
}

TEST_CASE("The chunk mesher keeps its resources alive", "[render][mesh]")
{
    auto resources = aurora::test::meshResourcesForTests();
    const std::weak_ptr<const MeshResources> watch = resources;
    const aurora::client::MeshFunction mesher = makeChunkMesher(std::move(resources));
    CHECK_FALSE(watch.expired());
    TestNeighbourhood blocks;
    blocks.set(1, 1, 1, state::kStone);
    CHECK(quadsOf(mesher(blocks.build())).size() == 6);
}

TEST_CASE("Texture layers are numbered from 1 and must fit 16 bits", "[render][mesh]")
{
    const std::vector<std::string> ids{"aurora:block/b", "aurora:block/a"};
    const std::optional<TextureLayers> layers = assignTextureLayers(ids);
    REQUIRE(layers);
    CHECK(layers->layerOf.at("aurora:block/b") == 1);
    CHECK(layers->layerOf.at("aurora:block/a") == 2);
    CHECK(layers->layerCount == 3);

    std::vector<std::string> many(kMaxTextureLayers - 1);
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i] = "t" + std::to_string(i);
    }
    CHECK(assignTextureLayers(many)); // 65,535 textures + layer 0 = 65,536 layers.
    many.push_back("one_more");
    CHECK_FALSE(assignTextureLayers(many));
}

TEST_CASE("Mesh resources follow each block's render layer and textures", "[render][mesh]")
{
    using aurora::data::BlockDefinition;
    using aurora::data::RenderLayer;
    using aurora::data::ResourceId;
    const auto block = [](std::string_view id, RenderLayer render, std::string_view texture) {
        BlockDefinition definition;
        definition.id = *ResourceId::parse(id);
        definition.render = render;
        definition.faceTextures.fill(*ResourceId::parse(texture));
        return definition;
    };
    std::vector<BlockDefinition> blocks{block("aurora:stone", RenderLayer::Opaque, "aurora:block/stone"),
                                        block("aurora:glass", RenderLayer::Cutout, "aurora:block/glass"),
                                        block("aurora:water", RenderLayer::Translucent, "aurora:block/water")};
    blocks[0].faceTextures[1] = *ResourceId::parse("aurora:block/stone_top");
    std::vector<aurora::data::LoadIssue> issues;
    const auto registry = aurora::data::BlockRegistry::create(std::move(blocks), issues);
    REQUIRE(registry);

    const std::vector<std::string> ids{"aurora:block/glass", "aurora:block/stone", "aurora:block/stone_top"};
    const auto resources = buildMeshResources(*registry, *assignTextureLayers(ids));
    REQUIRE(resources->states.size() == registry->stateCount());
    const auto look = [&](std::string_view id) { return resources->states[*registry->parseState(id).state]; };

    CHECK(look("aurora:air").material == FaceMaterial::None);
    CHECK_FALSE(look("aurora:air").occludes);
    CHECK(look("aurora:unknown").material == FaceMaterial::Opaque);
    CHECK(look("aurora:unknown").layers[0] == kMissingTextureLayer);
    CHECK(look("aurora:stone").occludes);
    CHECK(look("aurora:stone").layers[0] == 2);
    CHECK(look("aurora:stone").layers[1] == 3); // Its own top texture.
    CHECK(look("aurora:glass").material == FaceMaterial::Cutout);
    CHECK_FALSE(look("aurora:glass").occludes);
    CHECK(look("aurora:water").material == FaceMaterial::Cutout); // Until translucency comes with P0-10.
    CHECK(look("aurora:water").layers[0] == kMissingTextureLayer); // No layer for its texture.
}
