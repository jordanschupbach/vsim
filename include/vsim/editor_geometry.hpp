#pragma once

#include <vsim/scene.hpp>

#include <optional>

namespace vsim {
// Screen coordinates are normalized to the viewport, top-left (0,0).
// Returns the nearest visible mesh triangle between the camera's clip planes.
[[nodiscard]] std::optional<EntityId> pick_entity(const Scene& scene, const Transform& camera,
                                                  const Camera& lens, float aspect,
                                                  glm::vec2 screen);
} // namespace vsim
