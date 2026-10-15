#pragma once

#include <vsim/scene.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vsim {
// Generalized coordinate of one joint: an angle in radians (hinge) or a
// displacement in meters (slider), with its time derivative.
struct JointState {
    float position{0.0F};
    float velocity{0.0F};
};

// A saved copy of a world's simulation state: body poses and velocities,
// contact caches, and constraint warm starts. Restoring it rewinds the world
// (and its scene) without rebuilding any Jolt objects, which is how episodic
// users such as reinforcement learning environments reset cheaply.
class PhysicsState {
  public:
    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

  private:
    friend class PhysicsWorld;
    std::string data_;
    std::vector<std::pair<EntityId, std::uint32_t>> bodies_; // Entity and Jolt body index.
    std::vector<std::pair<EntityId, std::uint64_t>> joints_; // Entity and constraint serial.
};

// A scene owns authored/state data; the world owns all Jolt objects. Single-threaded.
// External edits are reconciled before stepping, querying, or applying forces.
class PhysicsWorld {
  public:
    explicit PhysicsWorld(Scene& scene);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void sync();
    void step(double delta);
    // Restoring requires the same bodies and joints as when the state was saved
    // (std::invalid_argument otherwise); the scene receives the restored poses.
    [[nodiscard]] PhysicsState save_state();
    void restore_state(const PhysicsState& state);
    void apply_impulse(EntityId id, glm::vec3 impulse);
    // Joint coordinate of the entity's joint, measured from the authored pose.
    [[nodiscard]] JointState joint_state(EntityId id);
    // Moves the entity and every body jointed below it so the joint takes the
    // given coordinate and rate; other joints keep their coordinates.
    void set_joint_state(EntityId id, float position, float velocity);
    // Torque (N m, hinge) or force (N, slider) along the joint axis, applied to
    // the entity and oppositely to its parent, consumed by the next step.
    void apply_joint_force(EntityId id, float value);
    [[nodiscard]] bool touching(EntityId first, EntityId second);
    [[nodiscard]] bool grounded(EntityId id);
    [[nodiscard]] std::size_t body_count() const;
    [[nodiscard]] std::size_t joint_count() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
