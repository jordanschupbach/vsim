#pragma once

#include <vsim/scene.hpp>

#include <deque>

namespace vsim {
class Editor {
  public:
    explicit Editor(Scene scene = {});
    [[nodiscard]] Json execute(const Json& request);

  private:
    Scene scene_;
    std::deque<Scene> undo_;
    std::deque<Scene> redo_;
    void commit(Scene scene);
};
} // namespace vsim
