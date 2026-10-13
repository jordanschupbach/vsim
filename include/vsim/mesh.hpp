#pragma once

#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace vsim {
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};
// All primitives fit in [-0.5, 0.5]; triangles use outward counterclockwise winding.
[[nodiscard]] std::vector<Vertex> primitive_mesh(std::string_view primitive);
} // namespace vsim
