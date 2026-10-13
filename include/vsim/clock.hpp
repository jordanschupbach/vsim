#pragma once

#include <cstddef>

namespace vsim {
struct TickBatch {
    std::size_t steps;
    double alpha;
    double dropped_seconds;
};

class FixedClock {
  public:
    explicit FixedClock(double step = 1.0 / 60.0, std::size_t max_steps = 8);
    [[nodiscard]] TickBatch advance(double elapsed);
    [[nodiscard]] double step() const noexcept;

  private:
    double step_;
    std::size_t max_steps_;
    double remainder_{0.0};
};
} // namespace vsim
