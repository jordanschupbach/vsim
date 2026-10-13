#include <vsim/mesh.hpp>

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace vsim {
namespace {
void triangle(std::vector<Vertex>& out, glm::vec3 a, glm::vec3 b, glm::vec3 c,
              bool smooth = false) {
    auto normal = glm::cross(b - a, c - a);
    if (glm::length(normal) < 0.000001F)
        return;
    if (glm::dot(normal, a + b + c) < 0) {
        std::swap(b, c);
        normal = -normal;
    }
    normal = glm::normalize(normal);
    for (auto p : {a, b, c})
        out.push_back({p, smooth ? glm::normalize(p) : normal});
}
} // namespace

std::vector<Vertex> primitive_mesh(std::string_view primitive) {
    std::vector<Vertex> result;
    if (primitive == "box") {
        for (int axis = 0; axis < 3; ++axis) {
            for (float sign : {-1.0F, 1.0F}) {
                std::array<glm::vec3, 4> corners{};
                const std::array<glm::vec2, 4> coordinates = {
                    glm::vec2(-0.5F, -0.5F), {0.5F, -0.5F}, {0.5F, 0.5F}, {-0.5F, 0.5F}};
                for (std::size_t i = 0; i < corners.size(); ++i) {
                    corners[i][axis] = sign * 0.5F;
                    corners[i][(axis + 1) % 3] = coordinates[i].x;
                    corners[i][(axis + 2) % 3] = coordinates[i].y;
                }
                triangle(result, corners[0], corners[1], corners[2]);
                triangle(result, corners[0], corners[2], corners[3]);
            }
        }
    } else if (primitive == "crystal") {
        const std::array<glm::vec3, 4> ring = {
            glm::vec3(0.5F, 0, 0), {0, 0, 0.5F}, {-0.5F, 0, 0}, {0, 0, -0.5F}};
        for (std::size_t i = 0; i < ring.size(); ++i) {
            triangle(result, {0, 0.5F, 0}, ring[i], ring[(i + 1) % ring.size()]);
            triangle(result, {0, -0.5F, 0}, ring[i], ring[(i + 1) % ring.size()]);
        }
    } else if (primitive == "sphere") {
        constexpr int rings = 16;
        constexpr int sectors = 32;
        auto point = [](int row, int column) {
            const float theta = glm::pi<float>() * static_cast<float>(row) / rings;
            const float phi = glm::two_pi<float>() * static_cast<float>(column) / sectors;
            return glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta),
                             std::sin(theta) * std::sin(phi)) *
                   0.5F;
        };
        for (int row = 0; row < rings; ++row) {
            for (int column = 0; column < sectors; ++column) {
                const auto a = point(row, column), b = point(row + 1, column);
                const auto c = point(row + 1, column + 1), d = point(row, column + 1);
                triangle(result, a, b, c, true);
                triangle(result, a, c, d, true);
            }
        }
    } else {
        throw std::invalid_argument("unknown mesh primitive");
    }
    return result;
}
} // namespace vsim
