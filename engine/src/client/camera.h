#pragma once

#include "world/coordinates.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace aurora::client {

// Which movement keys are held this frame.
struct MoveInput {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool fast = false;
};

// The view: a free-flying camera (move()) or one placed every frame, at the player's eyes (setPosition()).
// Position in double (block units), so it stays exact far from the origin.
//
// Angles in degrees. Yaw 0 looks north (-Z), 90 east (+X), 180 south (+Z), 270 west (-X). Pitch is positive
// upwards and kept within +-kMaxPitch.
//
// Camera-relative rendering: the view matrix holds only the rotation. Anything drawn or culled is first moved
// into camera space by relativeTo() (a subtraction in double, then float), so the shader never subtracts the
// camera position again and precision does not depend on how far from the origin the camera is.
class Camera {
public:
    static constexpr double kMaxPitch = 89.9;
    static constexpr double kSpeed = 10.0;       // Blocks per second.
    static constexpr double kFastSpeed = 40.0;   // With the fast key (Ctrl).
    static constexpr double kMaxFrameTime = 0.1; // Longer frames move as if 0.1 s had passed.
    static constexpr double kDegreesPerPixel = 0.12;
    static constexpr float kVerticalFov = 70.0f; // Degrees.
    static constexpr float kNearPlane = 0.05f;

    Camera(const glm::dvec3& position, double yaw, double pitch);

    // Turns by a mouse movement in pixels (right and down are positive).
    void turn(double dxPixels, double dyPixels);
    // Moves for one frame. The direction is normalised, so diagonal movement is not faster.
    void move(const MoveInput& input, double frameSeconds);
    void setPosition(const glm::dvec3& position) { m_position = position; }

    const glm::dvec3& position() const { return m_position; }
    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    // Unit vector the camera looks along.
    glm::dvec3 forward() const;
    world::ChunkPos chunk() const;
    // Section index of the camera height (may lie outside 0..23 above or below the world).
    std::int32_t sectionY() const;
    // "north", "east", "south" or "west": the nearest compass direction of the yaw.
    const char* facing() const;

    // Rotation only, no translation (see above).
    glm::mat4 viewRotation() const;
    glm::mat4 projection(float aspect, float farPlane) const;

private:
    glm::dvec3 m_position;
    double m_yaw = 0.0;
    double m_pitch = 0.0;
};

// `point - camera` computed in double, then converted: exact for camera-relative drawing and culling.
glm::vec3 relativeTo(const glm::dvec3& point, const glm::dvec3& camera);

} // namespace aurora::client
