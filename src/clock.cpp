#include <vsim/clock.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vsim {
FixedClock::FixedClock(double step, std::size_t max_steps) : step_(step), max_steps_(max_steps) {
    if (!std::isfinite(step) || step < 0.000001 || step > 1.0 || max_steps == 0 ||
        max_steps > 10000) {
        throw std::invalid_argument("invalid fixed clock configuration");
    }
}

TickBatch FixedClock::advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0.0) {
        throw std::invalid_argument("elapsed time must be finite and nonnegative");
    }
    const double capacity = step_ * static_cast<double>(max_steps_);
    const double accepted = std::min(elapsed, capacity);
    const double total = remainder_ + accepted;
    const auto steps = std::min(max_steps_, static_cast<std::size_t>(total / step_));
    remainder_ = total - static_cast<double>(steps) * step_;
    return {steps, remainder_ / step_, elapsed - accepted};
}

double FixedClock::step() const noexcept {
    return step_;
}
} // namespace vsim
