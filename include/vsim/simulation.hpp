#pragma once

#include <vsim/physics.hpp>
#include <vsim/script.hpp>

namespace vsim {
// Canonical update order shared by the interactive game, batch runtime, and editor.
class Simulation {
  public:
    explicit Simulation(Scene& scene);
    void tick(double delta, const InputFrame& input = {});
    [[nodiscard]] PhysicsWorld& physics() noexcept {
        return physics_;
    }

  private:
    PhysicsWorld physics_;
    ScriptSystem scripts_;
};
} // namespace vsim
