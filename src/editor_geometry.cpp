#include <vsim/editor_geometry.hpp>
#include <vsim/mesh.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace vsim {
namespace {
double intersect(glm::dvec3 origin, glm::dvec3 direction, const Vertex& a, const Vertex& b,
                 const Vertex& c) {
    const auto edge1 = glm::dvec3(b.position) - glm::dvec3(a.position);
    const auto edge2 = glm::dvec3(c.position) - glm::dvec3(a.position);
    const auto p = glm::cross(direction, edge2);
    const auto determinant = glm::dot(edge1, p);
    // Match the renderer's back-face culling, including positive nonuniform scale.
    if (determinant <= 0)
        return std::numeric_limits<double>::infinity();
    const auto inverse = 1.0F / determinant;
    const auto t = origin - glm::dvec3(a.position);
    const auto u = glm::dot(t, p) * inverse;
    if (u < 0 || u > 1)
        return std::numeric_limits<double>::infinity();
    const auto q = glm::cross(t, edge1);
    const auto v = glm::dot(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return std::numeric_limits<double>::infinity();
    const auto distance = glm::dot(edge2, q) * inverse;
    return distance >= 0 ? distance : std::numeric_limits<double>::infinity();
}
} // namespace

std::optional<EntityId> pick_entity(const Scene& scene, const Transform& camera, const Camera& lens,
                                    float aspect, glm::vec2 screen) {
    camera.validate();
    lens.validate();
    if (!std::isfinite(screen.x) || !std::isfinite(screen.y))
        throw std::invalid_argument("picking coordinates must be finite");
    (void)lens.projection(aspect); // Validate the same projection contract as rendering.
    if (screen.x < 0 || screen.y < 0 || screen.x > 1 || screen.y > 1)
        return std::nullopt;
    // Construct clip-plane points analytically. Inverting a float projection
    // loses the far-plane distance for otherwise valid large far/near ratios.
    const auto tangent = std::tan(glm::radians(static_cast<double>(lens.vertical_fov)) * 0.5);
    const glm::dvec3 camera_ray{(static_cast<double>(screen.x) * 2 - 1) * tangent * aspect,
                                (1 - static_cast<double>(screen.y) * 2) * tangent, -1};
    const auto rotation = glm::normalize(glm::dquat(camera.rotation));
    const auto origin =
        glm::dvec3(camera.position) + rotation * camera_ray * static_cast<double>(lens.near_plane);
    const auto direction = rotation * glm::normalize(camera_ray);
    auto closest =
        (static_cast<double>(lens.far_plane) - lens.near_plane) * glm::length(camera_ray);
    static const std::array<std::string_view, 3> names{"box", "sphere", "crystal"};
    static const std::array meshes{primitive_mesh("box"), primitive_mesh("sphere"),
                                   primitive_mesh("crystal")};
    std::optional<EntityId> selected;
    for (const auto id : scene.entities()) {
        const auto& entity = scene.get(id);
        if (!entity.mesh)
            continue;
        const auto model_inverse = glm::inverse(glm::dmat4(entity.transform.matrix()));
        const auto local_origin = glm::dvec3(model_inverse * glm::dvec4(origin, 1));
        // Do not normalize: the intersection parameter remains a world distance.
        const auto local_direction = glm::dvec3(model_inverse * glm::dvec4(direction, 0));
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (entity.mesh->primitive != names[index])
                continue;
            const auto& mesh = meshes[index];
            for (std::size_t i = 0; i < mesh.size(); i += 3) {
                const auto distance =
                    intersect(local_origin, local_direction, mesh[i], mesh[i + 1], mesh[i + 2]);
                if (distance < closest) {
                    closest = distance;
                    selected = id;
                }
            }
        }
    }
    return selected;
}
} // namespace vsim
