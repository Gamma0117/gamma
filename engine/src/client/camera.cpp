#include "client/camera.h"

#include "core/constants.h"
#include "entity/block_raycast.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace aurora::client {

namespace {

// Horizontal forward and right unit vectors for a yaw.
glm::dvec3 horizontalForward(double yaw)
{
    const double radians = glm::radians(yaw);
    return {std::sin(radians), 0.0, -std::cos(radians)};
}

glm::dvec3 horizontalRight(double yaw)
{
    const double radians = glm::radians(yaw);
    return {std::cos(radians), 0.0, std::sin(radians)};
}

} // namespace

Camera::Camera(const glm::dvec3& position, double yaw, double pitch)
    : m_position(position)
    , m_yaw(yaw)
    , m_pitch(pitch)
{
    turn(0.0, 0.0); // Brings the yaw into [0, 360) and clamps the pitch.
}

void Camera::turn(double dxPixels, double dyPixels)
{
    m_yaw = std::fmod(m_yaw + dxPixels * kDegreesPerPixel, 360.0);
    if (m_yaw < 0.0) {
        m_yaw += 360.0;
    }
    m_pitch = std::clamp(m_pitch - dyPixels * kDegreesPerPixel, -kMaxPitch, kMaxPitch);
}

void Camera::move(const MoveInput& input, double frameSeconds)
{
    const double seconds = std::clamp(frameSeconds, 0.0, kMaxFrameTime);
    const auto axis = [](bool positive, bool negative) { return (positive ? 1.0 : 0.0) - (negative ? 1.0 : 0.0); };
    glm::dvec3 direction = horizontalForward(m_yaw) * axis(input.forward, input.back) +
                           horizontalRight(m_yaw) * axis(input.right, input.left) +
                           glm::dvec3(0.0, axis(input.up, input.down), 0.0);
    if (glm::dot(direction, direction) == 0.0) {
        return;
    }
    direction = glm::normalize(direction);
    m_position += direction * (input.fast ? kFastSpeed : kSpeed) * seconds;
}

glm::dvec3 Camera::forward() const
{
    return entity::lookDirection(m_yaw, m_pitch);
}

world::ChunkPos Camera::chunk() const
{
    const auto block = [](double value) { return static_cast<std::int32_t>(std::floor(value)); };
    return world::chunkPosOf({block(m_position.x), 0, block(m_position.z)});
}

std::int32_t Camera::sectionY() const
{
    // Floor, so heights below the world give negative sections.
    return static_cast<std::int32_t>(std::floor((m_position.y - core::kWorldMinY) / core::kSectionSize));
}

const char* Camera::facing() const
{
    constexpr const char* kNames[] = {"north", "east", "south", "west"};
    return kNames[static_cast<int>(std::floor((m_yaw + 45.0) / 90.0)) % 4];
}

glm::mat4 Camera::viewRotation() const
{
    return glm::lookAt(glm::vec3(0.0f), glm::vec3(forward()), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection(float aspect, float farPlane) const
{
    return glm::perspective(glm::radians(kVerticalFov), aspect, kNearPlane, farPlane);
}

glm::vec3 relativeTo(const glm::dvec3& point, const glm::dvec3& camera)
{
    return glm::vec3(point - camera);
}

} // namespace aurora::client
