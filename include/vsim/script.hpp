#pragma once

#include <vsim/scene.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vsim {
class PhysicsWorld;
using InputFrame = std::map<std::string, float>;
void validate_input(const InputFrame& input);
[[nodiscard]] InputFrame input_from_json(const Json& data);
// Project scripts are trusted code. Library restrictions are not a security sandbox.
// Scene must outlive this system and must not be replaced while it is running.
class ScriptSystem {
  public:
    explicit ScriptSystem(Scene& scene, PhysicsWorld* physics = nullptr);
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;
    void tick(double delta, const InputFrame& input = {});
    [[nodiscard]] static const std::vector<std::string>& api_names();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
