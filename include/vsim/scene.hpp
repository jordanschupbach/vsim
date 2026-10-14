#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vsim {
using Json = nlohmann::json;
using EntityId = std::uint64_t;

struct Transform {
    glm::vec3 position{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
    [[nodiscard]] glm::mat4 matrix() const;
    void validate() const;
};

struct Camera {
    float vertical_fov{60.0F};
    float near_plane{0.1F};
    float far_plane{1000.0F};
    [[nodiscard]] glm::mat4 projection(float aspect) const;
    void validate() const;
};

struct Script {
    std::string source;
};

struct Mesh {
    std::string primitive{"box"}; // Unit-size box, sphere, or faceted crystal.
    glm::vec3 color{0.6F};        // Linear RGB albedo.
    float metallic{0.0F};
    float roughness{0.6F};
    float emissive{0.0F};
    void validate() const;
};

struct RigidBody {
    std::string motion{"static"}; // static, dynamic, kinematic
    std::string shape{"box"};     // box, sphere, or capsule; dimensions come from transform.scale
    float mass{1.0F};
    float friction{0.6F};
    float restitution{0.1F};
    float gravity_scale{1.0F};
    bool sensor{false};
    bool lock_rotation{false};            // Shorthand for allowed_rotation = {false, false, false}.
    glm::bvec3 allowed_translation{true}; // World axes along which a dynamic body may move.
    glm::bvec3 allowed_rotation{true};    // World axes about which a dynamic body may turn.
    glm::vec3 linear_velocity{0.0F};
    glm::vec3 angular_velocity{0.0F}; // World space, rad/s.
    void validate() const;
    bool operator==(const RigidBody&) const = default;
};

// Connects this entity's dynamic rigid body to a parent body (or the world when
// parent is 0). Anchor and axis are in this entity's rotated frame, in meters,
// ignoring scale. The joint coordinate is zero at the authored pose; the authored
// transforms must already satisfy the joint.
struct Joint {
    std::string type{"hinge"}; // hinge (rad) or slider (m)
    EntityId parent{0};
    glm::vec3 anchor{0.0F};
    glm::vec3 axis{0.0F, 0.0F, 1.0F};
    bool limited{false};
    float limit_min{0.0F}; // limit_min <= 0 <= limit_max; hinges within [-pi, pi]
    float limit_max{0.0F};
    float damping{0.0F}; // Viscous torque (N m s) or force (N s) opposing joint velocity.
    void validate() const;
    bool operator==(const Joint&) const = default;
};

struct Entity {
    std::string name{"Entity"};
    Transform transform;
    std::optional<Camera> camera;
    std::optional<Script> script;
    std::optional<Mesh> mesh;
    std::optional<RigidBody> rigid_body;
    std::optional<Joint> joint;
    void validate() const;
};

// IDs are scoped to a scene snapshot. Deleted IDs are never reused during its lifetime.
// References returned by get() must not be retained across scene replacement.
class Scene {
  public:
    static constexpr std::size_t max_entities = 10000;
    [[nodiscard]] EntityId create(Entity entity = {});
    void destroy(EntityId id);
    [[nodiscard]] bool alive(EntityId id) const noexcept;
    [[nodiscard]] const Entity& get(EntityId id) const;
    void set(EntityId id, Entity entity);
    [[nodiscard]] std::vector<EntityId> entities() const;
    [[nodiscard]] std::size_t size() const noexcept;
    void define_prefab(std::string name, Entity entity);
    [[nodiscard]] EntityId spawn(const std::string& name);
    [[nodiscard]] Json json() const;
    [[nodiscard]] static Scene from_json(const Json& data);

  private:
    EntityId next_id_{1};
    std::map<EntityId, Entity> entities_;
    std::map<std::string, Entity> prefabs_;
};

[[nodiscard]] Json entity_json(const Entity& entity);
[[nodiscard]] Entity entity_from_json(const Json& data);
[[nodiscard]] EntityId entity_id(const Json& data);
void require_keys(const Json& data, std::initializer_list<const char*> allowed);
} // namespace vsim
