#include <vsim/scene.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vsim {
namespace {
constexpr EntityId max_id = static_cast<EntityId>(std::numeric_limits<std::int64_t>::max());

void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos) {
        throw std::invalid_argument("names must contain 1..128 bytes and no NUL");
    }
    (void)Json(name).dump(); // Reject malformed UTF-8 before storing it in a scene.
}

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::mat4 checked_matrix(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const double value = matrix[column][row];
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                throw std::invalid_argument("matrix is outside the finite float range");
            }
        }
    }
    return glm::mat4(matrix);
}

float number(const Json& data) {
    if (!data.is_number()) {
        throw std::invalid_argument("expected a number");
    }
    const auto value = data.get<double>();
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument("number is outside the finite float range");
    }
    return static_cast<float>(value);
}

glm::vec3 vec3(const Json& data) {
    if (!data.is_array() || data.size() != 3) {
        throw std::invalid_argument("expected a three-number vector");
    }
    for (const auto& value : data) {
        if (!value.is_number()) {
            throw std::invalid_argument("vector entries must be numbers");
        }
    }
    return {number(data[0]), number(data[1]), number(data[2])};
}

glm::bvec3 bvec3(const Json& data) {
    if (!data.is_array() || data.size() != 3) {
        throw std::invalid_argument("expected three booleans");
    }
    for (const auto& value : data) {
        if (!value.is_boolean()) {
            throw std::invalid_argument("axis flags must be booleans");
        }
    }
    return {data[0].get<bool>(), data[1].get<bool>(), data[2].get<bool>()};
}

Json json(glm::vec3 v) {
    return {v.x, v.y, v.z};
}
Json json(glm::bvec3 v) {
    return {v.x, v.y, v.z};
}
} // namespace

void require_keys(const Json& data, std::initializer_list<const char*> allowed) {
    if (!data.is_object()) {
        throw std::invalid_argument("expected a JSON object");
    }
    for (const auto& [key, value] : data.items()) {
        (void)value;
        bool found = false;
        for (const auto* candidate : allowed) {
            found = found || key == candidate;
        }
        if (!found) {
            throw std::invalid_argument("unknown field: " + key);
        }
    }
}

EntityId entity_id(const Json& data) {
    if ((!data.is_number_unsigned() && !data.is_number_integer()) ||
        (data.is_number_integer() && !data.is_number_unsigned() && data.get<std::int64_t>() <= 0)) {
        throw std::invalid_argument("entity ID must be a positive integer");
    }
    const auto id = data.get<EntityId>();
    if (id == 0 || id > max_id) {
        throw std::invalid_argument("entity ID is out of range");
    }
    return id;
}

void Transform::validate() const {
    const float norm = glm::dot(rotation, rotation);
    if (!finite(position) || !finite(scale) ||
        glm::any(glm::lessThanEqual(scale, glm::vec3(0.0F))) || !std::isfinite(norm) ||
        std::abs(norm - 1.0F) > 0.0001F) {
        throw std::invalid_argument(
            "transform requires finite position, positive scale, unit quaternion");
    }
}

glm::mat4 Transform::matrix() const {
    validate();
    return checked_matrix(glm::translate(glm::dmat4(1.0), glm::dvec3(position)) *
                          glm::mat4_cast(glm::dquat(rotation)) *
                          glm::scale(glm::dmat4(1.0), glm::dvec3(scale)));
}

void Camera::validate() const {
    if (!std::isfinite(vertical_fov) || vertical_fov <= 1.0F || vertical_fov >= 179.0F ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0F ||
        far_plane <= near_plane) {
        throw std::invalid_argument("invalid camera frustum");
    }
}

glm::mat4 Camera::projection(float aspect) const {
    validate();
    if (!std::isfinite(aspect) || aspect <= 0.0F) {
        throw std::invalid_argument("aspect ratio must be finite and positive");
    }
    auto result = glm::perspectiveRH_ZO(
        glm::radians(static_cast<double>(vertical_fov)), static_cast<double>(aspect),
        static_cast<double>(near_plane), static_cast<double>(far_plane));
    result[1][1] *= -1.0; // Vulkan framebuffer coordinates.
    return checked_matrix(result);
}

void Entity::validate() const {
    validate_name(name);
    transform.validate();
    if (camera) {
        camera->validate();
    }
    if (script && script->source.size() > 1024 * 1024) {
        throw std::invalid_argument("script source exceeds 1 MiB");
    }
    if (script) {
        (void)Json(script->source).dump();
    }
    if (mesh)
        mesh->validate();
    if (rigid_body) {
        rigid_body->validate();
        if (glm::any(glm::lessThan(transform.scale, glm::vec3(0.02F))) ||
            glm::any(glm::greaterThan(transform.scale, glm::vec3(1000.0F))) ||
            glm::any(glm::greaterThan(glm::abs(transform.position), glm::vec3(100000.0F)))) {
            throw std::invalid_argument(
                "rigid bodies require scale in [0.02,1000] and position within +/-100000");
        }
        if (rigid_body->shape == "sphere" &&
            (std::abs(transform.scale.x - transform.scale.y) > 0.00001F ||
             std::abs(transform.scale.x - transform.scale.z) > 0.00001F)) {
            throw std::invalid_argument("sphere colliders require uniform scale");
        }
        if (rigid_body->shape == "capsule" &&
            (std::abs(transform.scale.x - transform.scale.z) > 0.00001F ||
             transform.scale.y <= transform.scale.x)) {
            throw std::invalid_argument(
                "capsule colliders require equal x/z scale (diameter) and a longer y scale");
        }
    }
    if (joint) {
        joint->validate();
        if (!rigid_body || rigid_body->motion != "dynamic") {
            throw std::invalid_argument("joints require a dynamic rigid body");
        }
    }
}

void Joint::validate() const {
    if ((type != "hinge" && type != "slider") || parent > max_id || !finite(anchor) ||
        glm::any(glm::greaterThan(glm::abs(anchor), glm::vec3(1000.0F))) || !finite(axis) ||
        glm::length(axis) < 0.000001F || !std::isfinite(limit_min) || !std::isfinite(limit_max) ||
        limit_min > 0 || limit_max < 0 ||
        (type == "hinge" && (limit_min < -glm::pi<float>() || limit_max > glm::pi<float>())) ||
        (type == "slider" && (limit_min < -1000.0F || limit_max > 1000.0F)) ||
        !std::isfinite(damping) || damping < 0 || damping > 1000000.0F) {
        throw std::invalid_argument("invalid joint type, parent, anchor, axis, limits, or damping");
    }
}

void Mesh::validate() const {
    if ((primitive != "box" && primitive != "sphere" && primitive != "crystal") || !finite(color) ||
        glm::any(glm::lessThan(color, glm::vec3(0.0F))) ||
        glm::any(glm::greaterThan(color, glm::vec3(1.0F))) || !std::isfinite(metallic) ||
        metallic < 0 || metallic > 1 || !std::isfinite(roughness) || roughness < 0.04F ||
        roughness > 1 || !std::isfinite(emissive) || emissive < 0 || emissive > 10) {
        throw std::invalid_argument("invalid mesh primitive or material");
    }
}

void RigidBody::validate() const {
    if ((motion != "static" && motion != "dynamic" && motion != "kinematic") ||
        (shape != "box" && shape != "sphere" && shape != "capsule") || !std::isfinite(mass) ||
        mass < 0.001F || mass > 1000000 || !std::isfinite(friction) || friction < 0 ||
        friction > 1 || !std::isfinite(restitution) || restitution < 0 || restitution > 1 ||
        !std::isfinite(gravity_scale) || gravity_scale < 0 || gravity_scale > 4 ||
        !finite(linear_velocity) ||
        glm::any(glm::greaterThan(glm::abs(linear_velocity), glm::vec3(500.0F))) ||
        !finite(angular_velocity) ||
        glm::any(glm::greaterThan(glm::abs(angular_velocity), glm::vec3(100.0F))) ||
        (motion == "static" &&
         (linear_velocity != glm::vec3(0) || angular_velocity != glm::vec3(0)))) {
        throw std::invalid_argument(
            "invalid rigid body motion, shape, material, mass, or velocity");
    }
    if (motion == "dynamic" && !glm::any(allowed_translation) &&
        (lock_rotation || !glm::any(allowed_rotation))) {
        throw std::invalid_argument("dynamic bodies need at least one free degree of freedom");
    }
}

void Scene::touch() noexcept {
    static std::atomic<std::uint64_t> next_revision{1};
    revision_ = next_revision.fetch_add(1, std::memory_order_relaxed);
}

EntityId Scene::create(Entity entity) {
    entity.validate();
    if (entities_.size() >= max_entities || next_id_ >= max_id) {
        throw std::length_error("scene entity capacity exhausted");
    }
    const auto id = next_id_;
    entities_.emplace(id, std::move(entity));
    ++next_id_;
    touch();
    return id;
}

void Scene::destroy(EntityId id) {
    if (entities_.erase(id) != 1) {
        throw std::out_of_range("entity does not exist");
    }
    touch();
}

bool Scene::alive(EntityId id) const noexcept {
    return entities_.contains(id);
}

const Entity& Scene::get(EntityId id) const {
    return entities_.at(id);
}

void Scene::set(EntityId id, Entity entity) {
    entity.validate();
    entities_.at(id) = std::move(entity);
    touch();
}

void Scene::set_motion(EntityId id, glm::vec3 position, glm::quat rotation,
                       glm::vec3 linear_velocity, glm::vec3 angular_velocity) {
    auto& entity = entities_.at(id);
    if (!entity.rigid_body)
        throw std::invalid_argument("motion state requires a rigid body");
    auto transform = entity.transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.validate();
    auto body = *entity.rigid_body;
    body.linear_velocity = linear_velocity;
    body.angular_velocity = angular_velocity;
    body.validate();
    entity.transform = transform;
    entity.rigid_body->linear_velocity = linear_velocity;
    entity.rigid_body->angular_velocity = angular_velocity;
    touch();
}

std::vector<EntityId> Scene::entities() const {
    std::vector<EntityId> result;
    result.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        (void)entity;
        result.push_back(id);
    }
    return result;
}

std::size_t Scene::size() const noexcept {
    return entities_.size();
}

void Scene::define_prefab(std::string name, Entity entity) {
    validate_name(name);
    entity.validate();
    if (!prefabs_.contains(name) && prefabs_.size() >= max_entities) {
        throw std::length_error("prefab capacity exhausted");
    }
    prefabs_.insert_or_assign(std::move(name), std::move(entity));
}

EntityId Scene::spawn(const std::string& name) {
    return create(prefabs_.at(name));
}

Json entity_json(const Entity& entity) {
    entity.validate();
    const auto& t = entity.transform;
    Json data = {{"name", entity.name},
                 {"transform",
                  {{"position", {t.position.x, t.position.y, t.position.z}},
                   {"rotation", {t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z}},
                   {"scale", {t.scale.x, t.scale.y, t.scale.z}}}}};
    if (entity.camera) {
        data["camera"] = {{"vertical_fov", entity.camera->vertical_fov},
                          {"near_plane", entity.camera->near_plane},
                          {"far_plane", entity.camera->far_plane}};
    }
    if (entity.script) {
        data["script"] = {{"source", entity.script->source}};
    }
    if (entity.mesh) {
        const auto& m = *entity.mesh;
        data["mesh"] = {{"primitive", m.primitive},
                        {"color", {m.color.x, m.color.y, m.color.z}},
                        {"metallic", m.metallic},
                        {"roughness", m.roughness},
                        {"emissive", m.emissive}};
    }
    if (entity.rigid_body) {
        const auto& b = *entity.rigid_body;
        data["rigid_body"] = {{"motion", b.motion},
                              {"shape", b.shape},
                              {"mass", b.mass},
                              {"friction", b.friction},
                              {"restitution", b.restitution},
                              {"gravity_scale", b.gravity_scale},
                              {"sensor", b.sensor},
                              {"lock_rotation", b.lock_rotation},
                              {"allowed_translation", json(b.allowed_translation)},
                              {"allowed_rotation", json(b.allowed_rotation)},
                              {"linear_velocity", json(b.linear_velocity)},
                              {"angular_velocity", json(b.angular_velocity)}};
    }
    if (entity.joint) {
        const auto& j = *entity.joint;
        data["joint"] = {{"type", j.type},           {"parent", j.parent},
                         {"anchor", json(j.anchor)}, {"axis", json(j.axis)},
                         {"limited", j.limited},     {"limit_min", j.limit_min},
                         {"limit_max", j.limit_max}, {"damping", j.damping}};
    }
    return data;
}

Entity entity_from_json(const Json& data) {
    require_keys(data, {"name", "transform", "camera", "script", "mesh", "rigid_body", "joint"});
    Entity entity;
    if (data.contains("name")) {
        entity.name = data.at("name").get<std::string>();
    }
    if (data.contains("transform")) {
        const auto& t = data.at("transform");
        require_keys(t, {"position", "rotation", "scale"});
        if (t.contains("position")) {
            entity.transform.position = vec3(t.at("position"));
        }
        if (t.contains("scale")) {
            entity.transform.scale = vec3(t.at("scale"));
        }
        if (t.contains("rotation")) {
            const auto& q = t.at("rotation");
            if (!q.is_array() || q.size() != 4) {
                throw std::invalid_argument("rotation must be [w, x, y, z]");
            }
            entity.transform.rotation = {number(q[0]), number(q[1]), number(q[2]), number(q[3])};
        }
    }
    if (data.contains("camera")) {
        const auto& c = data.at("camera");
        require_keys(c, {"vertical_fov", "near_plane", "far_plane"});
        entity.camera = Camera{};
        if (c.contains("vertical_fov"))
            entity.camera->vertical_fov = number(c.at("vertical_fov"));
        if (c.contains("near_plane"))
            entity.camera->near_plane = number(c.at("near_plane"));
        if (c.contains("far_plane"))
            entity.camera->far_plane = number(c.at("far_plane"));
    }
    if (data.contains("script")) {
        require_keys(data.at("script"), {"source"});
        entity.script = Script{data.at("script").at("source").get<std::string>()};
    }
    if (data.contains("mesh")) {
        const auto& m = data.at("mesh");
        require_keys(m, {"primitive", "color", "metallic", "roughness", "emissive"});
        entity.mesh = Mesh{};
        if (m.contains("primitive"))
            entity.mesh->primitive = m.at("primitive").get<std::string>();
        if (m.contains("color"))
            entity.mesh->color = vec3(m.at("color"));
        if (m.contains("metallic"))
            entity.mesh->metallic = number(m.at("metallic"));
        if (m.contains("roughness"))
            entity.mesh->roughness = number(m.at("roughness"));
        if (m.contains("emissive"))
            entity.mesh->emissive = number(m.at("emissive"));
    }
    if (data.contains("rigid_body")) {
        const auto& b = data.at("rigid_body");
        require_keys(b, {"motion", "shape", "mass", "friction", "restitution", "gravity_scale",
                         "sensor", "lock_rotation", "allowed_translation", "allowed_rotation",
                         "linear_velocity", "angular_velocity"});
        entity.rigid_body = RigidBody{};
        auto& body = *entity.rigid_body;
        if (b.contains("motion"))
            body.motion = b.at("motion").get<std::string>();
        if (b.contains("shape"))
            body.shape = b.at("shape").get<std::string>();
        if (b.contains("mass"))
            body.mass = number(b.at("mass"));
        if (b.contains("friction"))
            body.friction = number(b.at("friction"));
        if (b.contains("restitution"))
            body.restitution = number(b.at("restitution"));
        if (b.contains("gravity_scale"))
            body.gravity_scale = number(b.at("gravity_scale"));
        if (b.contains("sensor"))
            body.sensor = b.at("sensor").get<bool>();
        if (b.contains("lock_rotation"))
            body.lock_rotation = b.at("lock_rotation").get<bool>();
        if (b.contains("allowed_translation"))
            body.allowed_translation = bvec3(b.at("allowed_translation"));
        if (b.contains("allowed_rotation"))
            body.allowed_rotation = bvec3(b.at("allowed_rotation"));
        if (b.contains("linear_velocity"))
            body.linear_velocity = vec3(b.at("linear_velocity"));
        if (b.contains("angular_velocity"))
            body.angular_velocity = vec3(b.at("angular_velocity"));
    }
    if (data.contains("joint")) {
        const auto& j = data.at("joint");
        require_keys(j, {"type", "parent", "anchor", "axis", "limited", "limit_min", "limit_max",
                         "damping"});
        entity.joint = Joint{};
        auto& joint = *entity.joint;
        if (j.contains("type"))
            joint.type = j.at("type").get<std::string>();
        if (j.contains("parent")) {
            const auto& parent = j.at("parent");
            if (!parent.is_number_integer() ||
                (parent.is_number_integer() && !parent.is_number_unsigned() &&
                 parent.get<std::int64_t>() < 0)) {
                throw std::invalid_argument("joint parent must be a non-negative integer ID");
            }
            joint.parent = parent.get<EntityId>();
        }
        if (j.contains("anchor"))
            joint.anchor = vec3(j.at("anchor"));
        if (j.contains("axis"))
            joint.axis = vec3(j.at("axis"));
        if (j.contains("limited"))
            joint.limited = j.at("limited").get<bool>();
        if (j.contains("limit_min"))
            joint.limit_min = number(j.at("limit_min"));
        if (j.contains("limit_max"))
            joint.limit_max = number(j.at("limit_max"));
        if (j.contains("damping"))
            joint.damping = number(j.at("damping"));
    }
    entity.validate();
    return entity;
}

Json Scene::json() const {
    Json data = {{"version", 1},
                 {"next_id", next_id_},
                 {"entities", Json::array()},
                 {"prefabs", Json::object()}};
    for (const auto& [id, entity] : entities_) {
        data["entities"].push_back({{"id", id}, {"components", entity_json(entity)}});
    }
    for (const auto& [name, entity] : prefabs_) {
        data["prefabs"][name] = entity_json(entity);
    }
    return data;
}

Scene Scene::from_json(const Json& data) {
    require_keys(data, {"version", "next_id", "entities", "prefabs"});
    if (!data.at("version").is_number_integer() || data.at("version") != 1) {
        throw std::invalid_argument("unsupported scene version");
    }
    Scene scene;
    const auto& entities = data.at("entities");
    if (!entities.is_array() || entities.size() > max_entities) {
        throw std::invalid_argument("invalid entity collection");
    }
    EntityId highest = 0;
    for (const auto& item : entities) {
        require_keys(item, {"id", "components"});
        const auto id = entity_id(item.at("id"));
        if (!scene.entities_.emplace(id, entity_from_json(item.at("components"))).second) {
            throw std::invalid_argument("duplicate entity ID");
        }
        highest = std::max(highest, id);
    }
    if (highest == max_id) {
        throw std::invalid_argument("scene IDs exhausted");
    }
    for (const auto& [id, entity] : scene.entities_) {
        if (entity.joint && entity.joint->parent != 0 &&
            (entity.joint->parent == id || !scene.entities_.contains(entity.joint->parent))) {
            throw std::invalid_argument("joint parent must be another entity in the scene");
        }
    }
    scene.next_id_ = data.contains("next_id") ? entity_id(data.at("next_id")) : highest + 1;
    if (scene.next_id_ <= highest) {
        throw std::invalid_argument("next_id must exceed all live IDs");
    }
    if (data.contains("prefabs")) {
        if (!data.at("prefabs").is_object()) {
            throw std::invalid_argument("prefabs must be an object");
        }
        for (const auto& [name, entity] : data.at("prefabs").items()) {
            scene.define_prefab(name, entity_from_json(entity));
        }
    }
    scene.touch();
    return scene;
}
} // namespace vsim
