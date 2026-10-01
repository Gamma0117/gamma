#include "client/camera.h"
#include "client/frustum.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <string>
#include <vector>

using namespace aurora::client;
using Catch::Approx;

namespace {

bool near(const glm::dvec3& a, const glm::dvec3& b, double tolerance = 1e-9)
{
    return glm::length(a - b) < tolerance;
}

// Which of a row of section boxes along the x axis a camera sees, the boxes given in world blocks.
std::vector<bool> visibleBoxes(const Camera& camera, const std::vector<glm::dvec3>& boxOrigins)
{
    const Frustum frustum(camera.projection(16.0f / 9.0f, 1000.0f) * camera.viewRotation());
    std::vector<bool> visible;
    for (const glm::dvec3& origin : boxOrigins) {
        const glm::vec3 min = relativeTo(origin, camera.position());
        visible.push_back(frustum.intersects(min, min + glm::vec3(16.0f)));
    }
    return visible;
}

} // namespace

TEST_CASE("Camera yaw 0 looks north and turns clockwise", "[client][camera]")
{
    const glm::dvec3 origin(0.0);
    CHECK(near(Camera(origin, 0.0, 0.0).forward(), {0.0, 0.0, -1.0}));
    CHECK(near(Camera(origin, 90.0, 0.0).forward(), {1.0, 0.0, 0.0}));
    CHECK(near(Camera(origin, 180.0, 0.0).forward(), {0.0, 0.0, 1.0}));
    CHECK(near(Camera(origin, 270.0, 0.0).forward(), {-1.0, 0.0, 0.0}));
    CHECK(Camera(origin, 0.0, 0.0).facing() == std::string("north"));
    CHECK(Camera(origin, 100.0, 0.0).facing() == std::string("east"));
    CHECK(Camera(origin, 200.0, 0.0).facing() == std::string("south"));
    CHECK(Camera(origin, 300.0, 0.0).facing() == std::string("west"));
    CHECK(Camera(origin, 350.0, 0.0).facing() == std::string("north"));
    CHECK(Camera(origin, -90.0, 0.0).yaw() == Approx(270.0));
    CHECK(Camera(origin, 0.0, 90.0).forward().y > 0.99);
}

TEST_CASE("Camera pitch stays within limits and yaw wraps", "[client][camera]")
{
    Camera camera(glm::dvec3(0.0), 350.0, 0.0);
    camera.turn(0.0, -100000.0); // Mouse far up.
    CHECK(camera.pitch() == Approx(Camera::kMaxPitch));
    camera.turn(0.0, 100000.0);
    CHECK(camera.pitch() == Approx(-Camera::kMaxPitch));
    camera.turn(20.0 / Camera::kDegreesPerPixel, 0.0);
    CHECK(camera.yaw() == Approx(10.0));
}

TEST_CASE("Camera moves relative to its yaw at a fixed speed", "[client][camera]")
{
    Camera camera(glm::dvec3(0.0), 90.0, -45.0); // Facing east, looking down: movement stays horizontal.
    camera.move({.forward = true}, 0.05);
    CHECK(near(camera.position(), {Camera::kSpeed * 0.05, 0.0, 0.0}));

    camera = Camera(glm::dvec3(0.0), 0.0, 0.0);
    camera.move({.right = true}, 0.1);
    CHECK(near(camera.position(), {Camera::kSpeed * 0.1, 0.0, 0.0}));
    camera.move({.up = true, .fast = true}, 0.1);
    CHECK(camera.position().y == Approx(Camera::kFastSpeed * 0.1));

    // Diagonal (forward + right + up) is no faster than straight.
    camera = Camera(glm::dvec3(0.0), 0.0, 0.0);
    camera.move({.forward = true, .right = true, .up = true}, 0.1);
    CHECK(glm::length(camera.position()) == Approx(Camera::kSpeed * 0.1));

    // A long stall moves only 0.1 s worth; nothing moves without time or with opposite keys.
    camera = Camera(glm::dvec3(0.0), 0.0, 0.0);
    camera.move({.forward = true}, 5.0);
    CHECK(glm::length(camera.position()) == Approx(Camera::kSpeed * Camera::kMaxFrameTime));
    camera.move({.forward = true}, -1.0);
    camera.move({.forward = true, .back = true}, 0.1);
    CHECK(glm::length(camera.position()) == Approx(Camera::kSpeed * Camera::kMaxFrameTime));
}

TEST_CASE("Camera chunk and section use floor rounding", "[client][camera]")
{
    CHECK(Camera({-0.5, 70.0, 15.9}, 0.0, 0.0).chunk() == aurora::world::ChunkPos{-1, 0});
    CHECK(Camera({16.0, 70.0, -16.0}, 0.0, 0.0).chunk() == aurora::world::ChunkPos{1, -1});
    CHECK(Camera({0.0, -64.0, 0.0}, 0.0, 0.0).sectionY() == 0);
    CHECK(Camera({0.0, -64.5, 0.0}, 0.0, 0.0).sectionY() == -1);
    CHECK(Camera({0.0, 319.9, 0.0}, 0.0, 0.0).sectionY() == 23);
    CHECK(Camera({0.0, 320.0, 0.0}, 0.0, 0.0).sectionY() == 24);
}

TEST_CASE("The view matrix holds no translation", "[client][camera]")
{
    const Camera camera({123456.5, 80.0, -98765.25}, 37.0, -20.0);
    const glm::mat4 view = camera.viewRotation();
    CHECK(view[3][0] == Approx(0.0f).margin(1e-6));
    CHECK(view[3][1] == Approx(0.0f).margin(1e-6));
    CHECK(view[3][2] == Approx(0.0f).margin(1e-6));
}

TEST_CASE("Frustum keeps boxes in front and drops boxes behind", "[client][camera]")
{
    const Camera camera(glm::dvec3(0.5, 70.0, 0.5), 0.0, 0.0); // Looking north (-Z).
    const Frustum frustum(camera.projection(16.0f / 9.0f, 500.0f) * camera.viewRotation());
    const auto box = [&](glm::dvec3 origin) {
        const glm::vec3 min = relativeTo(origin, camera.position());
        return frustum.intersects(min, min + glm::vec3(16.0f));
    };
    CHECK(box({-8.0, 62.0, -40.0}));      // Ahead.
    CHECK_FALSE(box({-8.0, 62.0, 30.0})); // Behind.
    CHECK_FALSE(box({300.0, 62.0, -40.0})); // Far to the right of a 70 degree view.
    CHECK_FALSE(box({-8.0, 62.0, -700.0})); // Beyond the far plane.
    CHECK(box({-8.0, 62.0, -8.0}));       // Around the camera.
}

TEST_CASE("Far from the origin the relative scene and culling match the scene at the origin", "[client][camera]")
{
    const glm::dvec3 offset(1000000.0, 0.0, -1000000.0);
    const Camera here({0.25, 64.5, 0.75}, 30.0, -10.0);
    const Camera far(here.position() + offset, 30.0, -10.0);

    std::vector<glm::dvec3> origins;
    for (int i = -20; i <= 20; ++i) {
        origins.push_back({i * 16.0, 48.0, -32.0});
    }
    std::vector<glm::dvec3> farOrigins;
    for (const glm::dvec3& origin : origins) {
        farOrigins.push_back(origin + offset);
    }

    for (std::size_t i = 0; i < origins.size(); ++i) {
        const glm::vec3 a = relativeTo(origins[i], here.position());
        const glm::vec3 b = relativeTo(farOrigins[i], far.position());
        CHECK(a == b); // Exact: the subtraction happens in double.
    }
    CHECK(visibleBoxes(here, origins) == visibleBoxes(far, farOrigins));
}
