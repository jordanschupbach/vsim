#pragma once

#include <vsim/scene.hpp>

#include <memory>

namespace vsim {
// A scene owns authored/state data; the world owns all Jolt objects. Single-threaded.
// External edits are reconciled before stepping or applying an impulse.
class PhysicsWorld {
  public:
    explicit PhysicsWorld(Scene& scene);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void sync();
    void step(double delta);
    void apply_impulse(EntityId id, glm::vec3 impulse);
    [[nodiscard]] bool touching(EntityId first, EntityId second);
    [[nodiscard]] bool grounded(EntityId id);
    [[nodiscard]] std::size_t body_count() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
