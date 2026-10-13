#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vsim {
struct OverlayVertex {
    glm::vec2 position;
    glm::vec4 color;
};
[[nodiscard]] std::vector<OverlayVertex> overlay_vertices(std::uint32_t width, std::uint32_t height,
                                                          const std::array<std::string, 3>& lines);
} // namespace vsim
