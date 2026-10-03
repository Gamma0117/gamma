// Axis blocks (logs): which texture each face of the axis=x and axis=z states shows and how it is turned, checked
// against the geometry directly: a point on a turned block's face is turned back into the axis=y model, and the
// texture coordinate there must be the model face's own (u along its right, v down from its top, one texture per
// block).

#include "render/chunk_mesher.h"
#include "render/mesh_resources.h"
#include "render/mesh_vertex.h"

#include "mesh_test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

using namespace aurora;
using data::BlockFace;
using render::MeshVertex;

namespace {

using Vec = std::array<double, 3>;

// The face frames written out again here (normal, right, up of a viewer outside), not taken from the engine.
struct Frame {
    Vec normal;
    Vec right;
    Vec up;
};
constexpr std::array<Frame, 6> kFrames{{
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
}};

// From an axis state back to the axis=y model: the inverses of x: (x, y, z) -> (y, -x, z) and
// z: (x, y, z) -> (x, -z, y).
Vec turnBack(int axis, const Vec& v)
{
    switch (axis) {
    case 0:
        return {-v[1], v[0], v[2]};
    case 2:
        return {v[0], v[2], -v[1]};
    default:
        return v;
    }
}

double dot(const Vec& a, const Vec& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double fraction(double value)
{
    return value - std::floor(value);
}

int faceWithNormal(const Vec& normal)
{
    for (int face = 0; face < 6; ++face) {
        if (kFrames[static_cast<std::size_t>(face)].normal == normal) {
            return face;
        }
    }
    return -1;
}

// "aurora:test_log" (axis x/y/z, a different texture on every face of its model), "aurora:log" (end texture on top
// and bottom, one side texture), "aurora:stone".
struct Fixture {
    std::shared_ptr<const data::BlockRegistry> registry;
    std::shared_ptr<const render::MeshResources> resources;
    render::TextureLayers layers;

    Fixture()
    {
        const auto id = [](std::string_view text) { return *data::ResourceId::parse(text); };
        data::BlockDefinition test;
        test.id = id("aurora:test_log");
        test.properties = {data::BlockProperty{"axis", {"x", "y", "z"}}};
        test.defaultValues = {1};
        const char* names[] = {"aurora:block/down", "aurora:block/up",   "aurora:block/north",
                               "aurora:block/south", "aurora:block/west", "aurora:block/east"};
        for (std::size_t face = 0; face < 6; ++face) {
            test.faceTextures[face] = id(names[face]);
        }
        data::BlockDefinition log;
        log.id = id("aurora:log");
        log.properties = test.properties;
        log.defaultValues = {1};
        log.faceTextures.fill(id("aurora:block/log_side"));
        log.faceTextures[0] = id("aurora:block/log_end");
        log.faceTextures[1] = id("aurora:block/log_end");
        data::BlockDefinition stone;
        stone.id = id("aurora:stone");
        stone.faceTextures.fill(id("aurora:block/stone"));
        std::vector<data::LoadIssue> issues;
        registry = data::BlockRegistry::create({test, log, stone}, issues);
        std::vector<std::string> textures(std::begin(names), std::end(names));
        textures.insert(textures.end(), {"aurora:block/log_end", "aurora:block/log_side", "aurora:block/stone"});
        layers = *render::assignTextureLayers(textures);
        resources = render::buildMeshResources(*registry, layers);
    }

    data::BlockStateId state(std::string_view text) const { return *registry->parseState(text).state; }
    std::uint16_t layerOf(std::string_view texture) const { return layers.layerOf.find(texture)->second; }
};

struct Quad {
    std::array<MeshVertex, 4> vertices;
};

std::vector<Quad> quadsOf(const client::MeshData& mesh)
{
    std::vector<Quad> quads;
    for (std::size_t first = 0; first + 4 <= mesh.vertexCount(); first += 4) {
        Quad quad;
        for (std::size_t i = 0; i < 4; ++i) {
            quad.vertices[i] =
                render::unpackVertex(mesh.vertexWords[(first + i) * 2], mesh.vertexWords[(first + i) * 2 + 1]);
        }
        quads.push_back(quad);
    }
    return quads;
}

// The texture coordinate at section position `point` on `quad`, from the affine map through three of its corners.
std::array<double, 2> uvAt(const Quad& quad, const Vec& point)
{
    const int normalAxis = [&] {
        const Vec n = kFrames[quad.vertices[0].face].normal;
        return n[0] != 0 ? 0 : (n[1] != 0 ? 1 : 2);
    }();
    const int a = (normalAxis + 1) % 3;
    const int b = (normalAxis + 2) % 3;
    const auto coordinate = [](const MeshVertex& v, int axis) {
        return static_cast<double>(axis == 0 ? v.x : (axis == 1 ? v.y : v.z));
    };
    // Corners 0, 1, 2 of a rectangle are never on one line.
    const MeshVertex& p0 = quad.vertices[0];
    const MeshVertex& p1 = quad.vertices[1];
    const MeshVertex& p2 = quad.vertices[2];
    const double a1 = coordinate(p1, a) - coordinate(p0, a), b1 = coordinate(p1, b) - coordinate(p0, b);
    const double a2 = coordinate(p2, a) - coordinate(p0, a), b2 = coordinate(p2, b) - coordinate(p0, b);
    const double det = a1 * b2 - a2 * b1;
    const double qa = point[static_cast<std::size_t>(a)] - coordinate(p0, a);
    const double qb = point[static_cast<std::size_t>(b)] - coordinate(p0, b);
    const double s = (qa * b2 - a2 * qb) / det; // point = p0 + s (p1 - p0) + t (p2 - p0)
    const double t = (a1 * qb - qa * b1) / det;
    return {p0.u + s * (p1.u - p0.u) + t * (p2.u - p0.u), p0.v + s * (p1.v - p0.v) + t * (p2.v - p0.v)};
}

// Every quad of `mesh`: its layer is the model face's, and at points inside each block of the quad the texture
// coordinate is the model face's own. `axisOf(cell)` is the axis of the block in a section cell.
template <typename AxisOf, typename ModelLayer>
void checkTextures(const client::MeshData& mesh, const AxisOf& axisOf, const ModelLayer& modelLayer,
                   int& checkedPoints)
{
    for (const Quad& quad : quadsOf(mesh)) {
        const Frame& frame = kFrames[quad.vertices[0].face];
        // The quad's extent, and the cells behind it.
        Vec low{99, 99, 99};
        Vec high{-1, -1, -1};
        for (const MeshVertex& v : quad.vertices) {
            const Vec p{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
            for (std::size_t i = 0; i < 3; ++i) {
                low[i] = std::min(low[i], p[i]);
                high[i] = std::max(high[i], p[i]);
            }
        }
        const int normalAxis = frame.normal[0] != 0 ? 0 : (frame.normal[1] != 0 ? 1 : 2);
        for (double x = low[0]; x < std::max(high[0], low[0] + 1); x += 1) {
            for (double y = low[1]; y < std::max(high[1], low[1] + 1); y += 1) {
                for (double z = low[2]; z < std::max(high[2], low[2] + 1); z += 1) {
                    std::array<int, 3> cell{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
                    if (frame.normal[static_cast<std::size_t>(normalAxis)] > 0) {
                        --cell[static_cast<std::size_t>(normalAxis)]; // The plane is the cell's far side.
                    }
                    const int axis = axisOf(cell);
                    const Vec centre{cell[0] + 0.5, cell[1] + 0.5, cell[2] + 0.5};
                    const int source = faceWithNormal(turnBack(axis, frame.normal));
                    REQUIRE(source >= 0);
                    CHECK(quad.vertices[0].layer == modelLayer(source));
                    for (const auto& [f1, f2] : {std::array{0.3, 0.6}, std::array{0.8, 0.15}}) {
                        // A point on the face inside this cell, off its centre.
                        Vec point = centre;
                        const auto n = static_cast<std::size_t>(normalAxis);
                        point[n] += 0.5 * frame.normal[n];
                        const int a = (normalAxis + 1) % 3;
                        const int b = (normalAxis + 2) % 3;
                        point[static_cast<std::size_t>(a)] = cell[static_cast<std::size_t>(a)] + f1;
                        point[static_cast<std::size_t>(b)] = cell[static_cast<std::size_t>(b)] + f2;
                        const std::array<double, 2> uv = uvAt(quad, point);
                        Vec local{point[0] - centre[0], point[1] - centre[1], point[2] - centre[2]};
                        const Vec model = turnBack(axis, local);
                        const Frame& modelFrame = kFrames[static_cast<std::size_t>(source)];
                        INFO("face " << int(quad.vertices[0].face) << " axis " << axis << " cell " << cell[0] << ","
                                     << cell[1] << "," << cell[2]);
                        CHECK(fraction(uv[0]) == Catch::Approx(fraction(dot(model, modelFrame.right) + 0.5)));
                        CHECK(fraction(uv[1]) == Catch::Approx(fraction(-dot(model, modelFrame.up) + 0.5)));
                        ++checkedPoints;
                    }
                }
            }
        }
    }
}

} // namespace

TEST_CASE("The ends of an axis block face along its axis", "[render][axis]")
{
    using render::axisFaceSources;
    for (std::size_t face = 0; face < 6; ++face) {
        CHECK(axisFaceSources(1)[face].face == static_cast<BlockFace>(face));
        CHECK(axisFaceSources(1)[face].rotation == 0);
    }
    // The model's top lands up / east / south, its bottom down / west / north.
    CHECK(axisFaceSources(0)[static_cast<std::size_t>(BlockFace::East)].face == BlockFace::Up);
    CHECK(axisFaceSources(0)[static_cast<std::size_t>(BlockFace::West)].face == BlockFace::Down);
    CHECK(axisFaceSources(2)[static_cast<std::size_t>(BlockFace::South)].face == BlockFace::Up);
    CHECK(axisFaceSources(2)[static_cast<std::size_t>(BlockFace::North)].face == BlockFace::Down);
    // Every model face is used once per axis.
    for (const int axis : {0, 1, 2}) {
        std::array<int, 6> uses{};
        for (const render::FaceSource& source : axisFaceSources(axis)) {
            ++uses[static_cast<std::size_t>(source.face)];
        }
        CHECK(uses == std::array<int, 6>{1, 1, 1, 1, 1, 1});
    }

    const Fixture fixture;
    const render::StateLook& alongX = fixture.resources->states[fixture.state("aurora:log[axis=x]")];
    CHECK(alongX.layers[static_cast<std::size_t>(BlockFace::East)] == fixture.layerOf("aurora:block/log_end"));
    CHECK(alongX.layers[static_cast<std::size_t>(BlockFace::Up)] == fixture.layerOf("aurora:block/log_side"));
    const render::StateLook& stone = fixture.resources->states[fixture.state("aurora:stone")];
    CHECK(stone.rotations == std::array<std::uint8_t, 6>{});
}

TEST_CASE("All 18 faces of the three axes show the model's texture the right way round", "[render][axis]")
{
    const Fixture fixture;
    const auto modelLayer = [&](int source) {
        const char* names[] = {"aurora:block/down", "aurora:block/up",   "aurora:block/north",
                               "aurora:block/south", "aurora:block/west", "aurora:block/east"};
        return fixture.layerOf(names[source]);
    };
    int points = 0;
    for (const int axis : {0, 1, 2}) {
        const char* axisName[] = {"x", "y", "z"};
        INFO("axis " << axisName[axis]);
        // One block alone: six single-block quads.
        test::TestNeighbourhood single;
        single.set(5, 5, 5, fixture.state(std::string("aurora:test_log[axis=") + axisName[axis] + "]"));
        const client::MeshData mesh = render::meshSection(single.build(), *fixture.resources);
        CHECK(quadsOf(mesh).size() == 6);
        checkTextures(mesh, [&](const std::array<int, 3>&) { return axis; }, modelLayer, points);

        // A wall two wide, three high and one deep: merged rectangles of 2 x 3, 1 x 3 and 2 x 1 blocks, where the
        // texture must repeat every block, neither stretched nor mirrored.
        test::TestNeighbourhood wall;
        for (std::int32_t x = 4; x <= 5; ++x) {
            for (std::int32_t y = 4; y <= 6; ++y) {
                wall.set(x, y, 8, fixture.state(std::string("aurora:test_log[axis=") + axisName[axis] + "]"));
            }
        }
        const client::MeshData wallMesh = render::meshSection(wall.build(), *fixture.resources);
        CHECK(quadsOf(wallMesh).size() == 6);
        checkTextures(wallMesh, [&](const std::array<int, 3>&) { return axis; }, modelLayer, points);
    }
    CHECK(points > 0);
}

TEST_CASE("Faces that differ only in their texture's turn never merge", "[render][axis]")
{
    const Fixture fixture;
    const auto countFaces = [&](data::BlockStateId first, data::BlockStateId second, BlockFace face) {
        test::TestNeighbourhood pair;
        pair.set(4, 4, 8, first);
        pair.set(5, 4, 8, second);
        int quads = 0;
        for (const Quad& quad : quadsOf(render::meshSection(pair.build(), *fixture.resources))) {
            quads += quad.vertices[0].face == static_cast<std::uint8_t>(face) ? 1 : 0;
        }
        return quads;
    };
    const data::BlockStateId x = fixture.state("aurora:log[axis=x]");
    const data::BlockStateId z = fixture.state("aurora:log[axis=z]");
    // Both tops show the side texture: along x and along z it is turned differently.
    CHECK(fixture.resources->states[x].layers[1] == fixture.resources->states[z].layers[1]);
    CHECK(fixture.resources->states[x].rotations[1] != fixture.resources->states[z].rotations[1]);
    CHECK(countFaces(x, z, BlockFace::Up) == 2);
    CHECK(countFaces(x, x, BlockFace::Up) == 1);
    CHECK(countFaces(z, z, BlockFace::Up) == 1);
}

TEST_CASE("Turned textures stay right with corner shading and a flipped diagonal", "[render][axis]")
{
    const Fixture fixture;
    const auto modelLayer = [&](int source) {
        const char* names[] = {"aurora:block/down", "aurora:block/up",   "aurora:block/north",
                               "aurora:block/south", "aurora:block/west", "aurora:block/east"};
        return fixture.layerOf(names[source]);
    };
    const data::BlockStateId stone = fixture.state("aurora:stone");
    int points = 0;
    int shaded = 0;
    for (const int axis : {0, 2}) {
        const char* axisName[] = {"x", "y", "z"};
        test::TestNeighbourhood scene;
        scene.set(5, 5, 5, fixture.state(std::string("aurora:test_log[axis=") + axisName[axis] + "]"));
        // Occluders around one corner of each face darken it, so the corners differ and the diagonal turns.
        for (const auto& [x, y, z] : {std::array{6, 6, 5}, std::array{5, 6, 6}, std::array{6, 5, 6},
                                      std::array{4, 4, 5}, std::array{5, 4, 4}, std::array{4, 5, 4}}) {
            scene.set(x, y, z, stone);
        }
        const client::MeshData mesh = render::meshSection(scene.build(), *fixture.resources);
        for (const Quad& quad : quadsOf(mesh)) {
            bool varied = false;
            for (const MeshVertex& v : quad.vertices) {
                varied = varied || v.ao != quad.vertices[0].ao;
            }
            shaded += varied ? 1 : 0;
        }
        // Only the log's faces: the stones are not axis blocks.
        client::MeshData logOnly;
        for (const Quad& quad : quadsOf(mesh)) {
            if (quad.vertices[0].layer != fixture.layerOf("aurora:block/stone")) {
                for (const MeshVertex& v : quad.vertices) {
                    const std::array<std::uint32_t, 2> words = render::packVertex(v);
                    logOnly.vertexWords.insert(logOnly.vertexWords.end(), words.begin(), words.end());
                }
            }
        }
        checkTextures(logOnly, [&](const std::array<int, 3>&) { return axis; }, modelLayer, points);
    }
    CHECK(shaded > 0);
    CHECK(points > 0);
}

TEST_CASE("Axis=y and unturned blocks mesh exactly as before", "[render][axis]")
{
    const Fixture fixture;
    // A block with the same layers and no turns: the axis=y log's mesh, word for word.
    auto resources = std::make_shared<render::MeshResources>(*fixture.resources);
    const data::BlockStateId y = fixture.state("aurora:test_log[axis=y]");
    const data::BlockStateId plain = fixture.state("aurora:stone");
    resources->states[plain] = resources->states[y];
    REQUIRE(resources->states[y].rotations == std::array<std::uint8_t, 6>{});
    test::TestNeighbourhood a;
    test::TestNeighbourhood b;
    for (std::int32_t i = 0; i < 6; ++i) {
        a.set(3 + i, 4 + i % 2, 7, y);
        b.set(3 + i, 4 + i % 2, 7, plain);
    }
    const client::MeshData withLog = render::meshSection(a.build(), *resources);
    const client::MeshData withPlain = render::meshSection(b.build(), *resources);
    CHECK(withLog.vertexWords == withPlain.vertexWords);
    CHECK(withLog.indices == withPlain.indices);
}
