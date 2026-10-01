#include "client/frustum.h"

namespace aurora::client {

Frustum::Frustum(const glm::mat4& viewProjection)
{
    // Rows of the matrix (glm is column-major). OpenGL clip space keeps -w <= x, y, z <= w.
    const auto row = [&](int i) {
        return glm::vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]);
    };
    const glm::vec4 x = row(0);
    const glm::vec4 y = row(1);
    const glm::vec4 z = row(2);
    const glm::vec4 w = row(3);
    m_planes = {w + x, w - x, w + y, w - y, w + z, w - z};
}

bool Frustum::intersects(const glm::vec3& min, const glm::vec3& max) const
{
    for (const glm::vec4& plane : m_planes) {
        // The box corner furthest along the plane normal.
        const glm::vec3 corner(plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y,
                               plane.z >= 0.0f ? max.z : min.z);
        if (plane.x * corner.x + plane.y * corner.y + plane.z * corner.z + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace aurora::client
