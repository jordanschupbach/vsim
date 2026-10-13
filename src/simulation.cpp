#include <vsim/simulation.hpp>

#include <cmath>
#include <stdexcept>

namespace vsim {
Simulation::Simulation(Scene& scene) : physics_(scene), scripts_(scene, &physics_) {}

void Simulation::tick(double delta, const InputFrame& input) {
    if (!std::isfinite(delta) || delta <= 0 || delta > 1.0 / 30.0)
        throw std::invalid_argument("simulation delta must be in (0, 1/30] seconds");
    validate_input(input);
    physics_.sync();
    scripts_.tick(delta, input);
    physics_.step(delta);
}
} // namespace vsim
