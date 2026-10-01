#pragma once

#include <glm/glm.hpp>

#include <array>

namespace aurora::client {

// The six clip planes of a projection * view matrix, for culling boxes in the same (camera-relative) space.
class Frustum {
public:
    explicit Frustum(const glm::mat4& viewProjection);

    // False only when the box lies wholly outside one plane, so it can be skipped. Boxes near a corner of the
    // frustum may pass without being visible; that only costs a draw.
    bool intersects(const glm::vec3& min, const glm::vec3& max) const;

private:
    std::array<glm::vec4, 6> m_planes; // ax + by + cz + d >= 0 inside.
};

} // namespace aurora::client
