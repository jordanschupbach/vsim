#include <Jolt/Jolt.h> // Jolt requires its platform definitions before all other Jolt headers.

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/SimShapeFilter.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/RegisterTypes.h>

#include <vsim/physics.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace vsim {
namespace {
// Jolt requires one process-wide type registry. It owns no scene or simulation state.
class JoltRegistration {
  public:
    JoltRegistration() {
        if (JPH::Factory::sInstance)
            throw std::runtime_error("Jolt already has an external factory owner");
        JPH::RegisterDefaultAllocator();
        factory_ = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory_.get();
        JPH::RegisterTypes();
    }
    ~JoltRegistration() {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
    }

  private:
    std::unique_ptr<JPH::Factory> factory_;
};

void initialize_jolt() {
    static const JoltRegistration registration;
    (void)registration;
}

JPH::Vec3 jvec(glm::vec3 v) {
    return {v.x, v.y, v.z};
}
glm::vec3 gvec(JPH::Vec3 v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}
JPH::Quat jquat(glm::quat q) {
    // Serialized rotations allow rounding error; Jolt requires a tighter unit norm.
    q = glm::normalize(q);
    return {q.x, q.y, q.z, q.w};
}
glm::quat gquat(JPH::Quat q) {
    return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
}

class Layers final : public JPH::BroadPhaseLayerInterface,
                     public JPH::ObjectVsBroadPhaseLayerFilter,
                     public JPH::ObjectLayerPairFilter {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return 2;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));
    }
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a != 0 || b != 0;
    }
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a != 0 || b.GetValue() != 0;
    }
};

class OnlyBody final : public JPH::BodyFilter {
  public:
    explicit OnlyBody(JPH::BodyID id) : id_(id) {}
    bool ShouldCollide(const JPH::BodyID& id) const override {
        return id == id_;
    }

  private:
    JPH::BodyID id_;
};
class GroundFilter final : public JPH::IgnoreSingleBodyFilter {
  public:
    using IgnoreSingleBodyFilter::IgnoreSingleBodyFilter;
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        return !body.IsSensor();
    }
};

bool same_config(RigidBody a, const RigidBody& b) {
    a.linear_velocity = b.linear_velocity;
    a.angular_velocity = b.angular_velocity;
    return a == b;
}

JPH::EAllowedDOFs allowed_dofs(const RigidBody& body) {
    auto dofs = JPH::EAllowedDOFs::None;
    const std::array<JPH::EAllowedDOFs, 3> translation = {JPH::EAllowedDOFs::TranslationX,
                                                          JPH::EAllowedDOFs::TranslationY,
                                                          JPH::EAllowedDOFs::TranslationZ};
    const std::array<JPH::EAllowedDOFs, 3> rotation = {
        JPH::EAllowedDOFs::RotationX, JPH::EAllowedDOFs::RotationY, JPH::EAllowedDOFs::RotationZ};
    for (int axis = 0; axis < 3; ++axis) {
        if (body.allowed_translation[axis])
            dofs |= translation[static_cast<std::size_t>(axis)];
        if (body.allowed_rotation[axis] && !body.lock_rotation)
            dofs |= rotation[static_cast<std::size_t>(axis)];
    }
    return dofs;
}

// World-space joint frame derived from the child's current transform.
struct JointFrame {
    glm::vec3 anchor;
    glm::vec3 axis;
};
JointFrame joint_frame(const Entity& entity) {
    const auto rotation = glm::normalize(entity.transform.rotation);
    return {entity.transform.position + rotation * entity.joint->anchor,
            glm::normalize(rotation * entity.joint->axis)};
}
} // namespace

struct PhysicsWorld::Impl {
    struct Body {
        JPH::BodyID id;
        Transform transform;
        RigidBody component;
        bool grouped{false};
    };
    struct NativeJoint {
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
        Joint component;
        JPH::BodyID child;
        JPH::BodyID parent; // Invalid when attached to the world.
        bool hinge;
        bool parent_dynamic;  // Receives the reaction of joint forces.
        std::uint64_t serial; // Unique per constraint built by this world.
    };
    Scene& scene;
    Layers layers;
    JPH::SimShapeFilter shape_filter;
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImplWithMallocFallback allocator{8 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    std::map<EntityId, Body> bodies;
    std::map<EntityId, NativeJoint> joints;
    std::vector<JPH::Ref<JPH::GroupFilterTable>> filters;
    bool joints_changed{false};
    // Scene revision the bodies and joints were last reconciled with; sync() is
    // free while it matches.
    std::uint64_t synced_revision{0};
    std::uint64_t constraints_built{0};
    // The world is single-threaded, so body access skips Jolt's body locks.
    JPH::BodyInterface& bodies_interface() {
        return system.GetBodyInterfaceNoLock();
    }
    const JPH::BodyInterface& bodies_interface() const {
        return system.GetBodyInterfaceNoLock();
    }

    explicit Impl(Scene& source) : scene(source) {
        // One body mutex: the world is single-threaded, and Jolt locks every
        // mutex on each update (its default scales them with the core count).
        system.Init(static_cast<JPH::uint>(Scene::max_entities + 1), 1, 65536, 20000, layers,
                    layers, layers);
        system.SetGravity(JPH::Vec3(0, -9.81F, 0));
        // Use Jolt's identity filter. In 5.3, the null-filter fast path aliases a
        // ShapeFilter as SimShapeFilterWrapper, which fails UBSan vptr checks.
        system.SetSimShapeFilter(&shape_filter);
    }
    ~Impl() {
        for (auto& [id, joint] : joints) {
            (void)id;
            system.RemoveConstraint(joint.constraint);
        }
        joints.clear();
        for (const auto& [id, body] : bodies) {
            (void)id;
            bodies_interface().RemoveBody(body.id);
            bodies_interface().DestroyBody(body.id);
        }
    }
    Body create(EntityId id, const Entity& entity) {
        const auto& component = *entity.rigid_body;
        const auto& transform = entity.transform;
        JPH::RefConst<JPH::Shape> shape;
        if (component.shape == "sphere") {
            shape = new JPH::SphereShape(transform.scale.x * 0.5F);
        } else if (component.shape == "capsule") {
            const float radius = transform.scale.x * 0.5F;
            shape = new JPH::CapsuleShape(transform.scale.y * 0.5F - radius, radius);
        } else {
            const auto half = transform.scale * 0.5F;
            shape = new JPH::BoxShape(
                jvec(half), std::min({0.05F, half.x * 0.1F, half.y * 0.1F, half.z * 0.1F}));
        }
        const auto motion = component.motion == "static"    ? JPH::EMotionType::Static
                            : component.motion == "dynamic" ? JPH::EMotionType::Dynamic
                                                            : JPH::EMotionType::Kinematic;
        JPH::BodyCreationSettings settings(shape, jvec(transform.position),
                                           jquat(transform.rotation), motion,
                                           motion == JPH::EMotionType::Static ? 0 : 1);
        settings.mUserData = id;
        settings.mIsSensor = component.sensor;
        settings.mFriction = component.friction;
        settings.mRestitution = component.restitution;
        settings.mGravityFactor = component.gravity_scale;
        settings.mLinearVelocity = jvec(component.linear_velocity);
        settings.mAngularVelocity = jvec(component.angular_velocity);
        settings.mMaxLinearVelocity = 500.0F;
        settings.mMaxAngularVelocity = 100.0F;
        settings.mMotionQuality = motion == JPH::EMotionType::Dynamic && !component.sensor
                                      ? JPH::EMotionQuality::LinearCast
                                      : JPH::EMotionQuality::Discrete;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = component.mass;
        if (motion == JPH::EMotionType::Dynamic)
            settings.mAllowedDOFs = allowed_dofs(component);
        auto& interface = bodies_interface();
        const auto body = interface.CreateAndAddBody(settings, motion == JPH::EMotionType::Static
                                                                   ? JPH::EActivation::DontActivate
                                                                   : JPH::EActivation::Activate);
        if (body.IsInvalid())
            throw std::runtime_error("Jolt body capacity exhausted");
        return {body, transform, component, false};
    }
    void remove(JPH::BodyID body) {
        bodies_interface().RemoveBody(body);
        bodies_interface().DestroyBody(body);
    }
    void remove_joints_touching(JPH::BodyID body) {
        std::erase_if(joints, [&](auto& item) {
            if (item.second.child != body && item.second.parent != body)
                return false;
            system.RemoveConstraint(item.second.constraint);
            joints_changed = true;
            return true;
        });
    }
    JPH::BodyID parent_body(const Joint& joint) const {
        return joint.parent == 0 ? JPH::BodyID() : bodies.at(joint.parent).id;
    }
    void validate_joint(EntityId id, const Entity& entity) const {
        const auto parent = entity.joint->parent;
        if (parent != 0 &&
            (parent == id || !scene.alive(parent) || !scene.get(parent).rigid_body)) {
            throw std::invalid_argument("joint parent must be another entity with a rigid body");
        }
        auto current = parent;
        for (std::size_t depth = 0; current != 0; ++depth) {
            if (current == id || depth > scene.size())
                throw std::invalid_argument("joint chain forms a cycle");
            const auto& link = scene.get(current);
            current = link.joint ? link.joint->parent : 0;
        }
    }
    NativeJoint create_joint(EntityId id, const Entity& entity) {
        const auto& joint = *entity.joint;
        const auto frame = joint_frame(entity);
        const auto reference =
            std::abs(frame.axis.x) < 0.9F ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const auto normal = glm::normalize(glm::cross(frame.axis, reference));
        const auto child = bodies.at(id).id;
        const auto parent = parent_body(joint);
        const auto& lock = system.GetBodyLockInterfaceNoLock();
        auto* child_body = lock.TryGetBody(child);
        auto* parent_body =
            parent.IsInvalid() ? &JPH::Body::sFixedToWorld : lock.TryGetBody(parent);
        if (!child_body || !parent_body)
            throw std::runtime_error("joint bodies are not available");
        JPH::Ref<JPH::TwoBodyConstraint> constraint;
        if (joint.type == "hinge") {
            JPH::HingeConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = settings.mPoint2 = jvec(frame.anchor);
            settings.mHingeAxis1 = settings.mHingeAxis2 = jvec(frame.axis);
            settings.mNormalAxis1 = settings.mNormalAxis2 = jvec(normal);
            if (joint.limited) {
                settings.mLimitsMin = joint.limit_min;
                settings.mLimitsMax = joint.limit_max;
            }
            constraint = settings.Create(*parent_body, *child_body);
        } else {
            JPH::SliderConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mAutoDetectPoint = true;
            settings.SetSliderAxis(jvec(frame.axis));
            if (joint.limited) {
                settings.mLimitsMin = joint.limit_min;
                settings.mLimitsMax = joint.limit_max;
            }
            constraint = settings.Create(*parent_body, *child_body);
        }
        system.AddConstraint(constraint);
        joints_changed = true;
        const bool parent_dynamic =
            joint.parent != 0 && scene.get(joint.parent).rigid_body->motion == "dynamic";
        return {constraint,         joint, child, parent, joint.type == "hinge", parent_dynamic,
                ++constraints_built};
    }
    // Bodies connected by a joint never collide with each other.
    void rebuild_groups() {
        std::map<EntityId, EntityId> parent_of;
        auto find = [&](EntityId id) {
            while (parent_of.contains(id) && parent_of.at(id) != id)
                id = parent_of.at(id);
            return id;
        };
        for (const auto& [id, joint] : joints) {
            if (joint.component.parent == 0)
                continue;
            parent_of.try_emplace(id, id);
            parent_of.try_emplace(joint.component.parent, joint.component.parent);
            parent_of[find(id)] = find(joint.component.parent);
        }
        std::map<EntityId, std::vector<EntityId>> components;
        for (const auto& [id, link] : parent_of) {
            (void)link;
            components[find(id)].push_back(id);
        }
        filters.clear();
        struct Assignment {
            JPH::GroupFilterTable* table;
            JPH::CollisionGroup::GroupID group;
            JPH::CollisionGroup::SubGroupID sub;
        };
        std::map<EntityId, Assignment> assignments;
        JPH::CollisionGroup::GroupID group = 0;
        for (const auto& [root, members] : components) {
            (void)root;
            JPH::Ref<JPH::GroupFilterTable> table =
                new JPH::GroupFilterTable(static_cast<JPH::uint>(members.size()));
            for (std::size_t i = 0; i < members.size(); ++i)
                assignments[members[i]] = {table.GetPtr(), group,
                                           static_cast<JPH::CollisionGroup::SubGroupID>(i)};
            for (const auto member : members) {
                const auto found = joints.find(member);
                if (found != joints.end() && found->second.component.parent != 0)
                    table->DisableCollision(assignments.at(member).sub,
                                            assignments.at(found->second.component.parent).sub);
            }
            filters.push_back(table);
            ++group;
        }
        auto& interface = bodies_interface();
        for (auto& [id, body] : bodies) {
            const auto found = assignments.find(id);
            if (found != assignments.end()) {
                const auto& a = found->second;
                interface.SetCollisionGroup(body.id, JPH::CollisionGroup(a.table, a.group, a.sub));
                body.grouped = true;
            } else if (body.grouped) {
                interface.SetCollisionGroup(body.id, JPH::CollisionGroup());
                body.grouped = false;
            } else {
                continue;
            }
            // Sleeping bodies would otherwise keep ignoring (or overlapping) each other.
            if (body.component.motion != "static")
                interface.ActivateBody(body.id);
        }
        joints_changed = false;
    }
    // The entity and every body whose joint chain leads to it.
    std::vector<EntityId> subtree(EntityId root) const {
        std::set<EntityId> members{root};
        for (bool grown = true; grown;) {
            grown = false;
            for (const auto& [id, joint] : joints) {
                if (!members.contains(id) && members.contains(joint.component.parent)) {
                    members.insert(id);
                    grown = true;
                }
            }
        }
        return {members.begin(), members.end()};
    }
    const NativeJoint& joint_of(EntityId id) const {
        const auto found = joints.find(id);
        if (found == joints.end())
            throw std::invalid_argument("entity has no joint");
        return found->second;
    }
    JointState state(const NativeJoint& joint) const {
        const auto& interface = bodies_interface();
        const auto axis =
            glm::normalize(gquat(interface.GetRotation(joint.child)) * joint.component.axis);
        const auto child_position = gvec(interface.GetPosition(joint.child));
        glm::vec3 parent_angular(0), parent_linear(0);
        if (!joint.parent.IsInvalid()) {
            parent_angular = gvec(interface.GetAngularVelocity(joint.parent));
            parent_linear = gvec(interface.GetLinearVelocity(joint.parent)) +
                            glm::cross(parent_angular,
                                       child_position - gvec(interface.GetPosition(joint.parent)));
        }
        JointState result;
        if (joint.hinge) {
            result.position = static_cast<const JPH::HingeConstraint*>(joint.constraint.GetPtr())
                                  ->GetCurrentAngle();
            result.velocity =
                glm::dot(gvec(interface.GetAngularVelocity(joint.child)) - parent_angular, axis);
        } else {
            result.position = static_cast<const JPH::SliderConstraint*>(joint.constraint.GetPtr())
                                  ->GetCurrentPosition();
            result.velocity =
                glm::dot(gvec(interface.GetLinearVelocity(joint.child)) - parent_linear, axis);
        }
        return result;
    }
    void add_joint_force(EntityId id, const NativeJoint& joint, float value) {
        const auto axis = joint_frame(scene.get(id)).axis * value;
        auto& interface = bodies_interface();
        if (joint.hinge) {
            interface.AddTorque(joint.child, jvec(axis));
            if (joint.parent_dynamic)
                interface.AddTorque(joint.parent, jvec(-axis));
        } else {
            interface.AddForce(joint.child, jvec(axis));
            if (joint.parent_dynamic)
                interface.AddForce(joint.parent, jvec(-axis));
        }
    }
    void store(EntityId id, Body& body) {
        const auto& interface = bodies_interface();
        scene.set_motion(id, gvec(interface.GetPosition(body.id)),
                         gquat(interface.GetRotation(body.id)),
                         gvec(interface.GetLinearVelocity(body.id)),
                         gvec(interface.GetAngularVelocity(body.id)));
        const auto& entity = scene.get(id);
        body.transform.position = entity.transform.position;
        body.transform.rotation = entity.transform.rotation;
        body.component.linear_velocity = entity.rigid_body->linear_velocity;
        body.component.angular_velocity = entity.rigid_body->angular_velocity;
    }
    // Writes every body back to the scene; the scene then matches the world.
    void store_all() {
        for (auto& [id, body] : bodies)
            store(id, body);
        synced_revision = scene.revision();
    }
};

PhysicsWorld::PhysicsWorld(Scene& scene) {
    initialize_jolt();
    impl_ = std::make_unique<Impl>(scene);
    sync();
}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::sync() {
    auto& world = *impl_;
    if (world.synced_revision != 0 && world.scene.revision() == world.synced_revision)
        return;
    auto& interface = world.bodies_interface();
    std::erase_if(world.bodies, [&](const auto& item) {
        if (world.scene.alive(item.first) && world.scene.get(item.first).rigid_body)
            return false;
        world.remove_joints_touching(item.second.id);
        world.remove(item.second.id);
        return true;
    });
    for (const auto id : world.scene.entities()) {
        const auto& entity = world.scene.get(id);
        if (!entity.rigid_body)
            continue;
        auto found = world.bodies.find(id);
        if (found == world.bodies.end()) {
            auto native = world.create(id, entity);
            try {
                world.bodies.emplace(id, native);
            } catch (...) {
                world.remove(native.id);
                throw;
            }
            continue;
        }
        auto& body = found->second;
        if (!same_config(body.component, *entity.rigid_body) ||
            body.transform.scale != entity.transform.scale) {
            world.remove_joints_touching(body.id);
            auto replacement = world.create(id, entity);
            world.remove(body.id);
            body = replacement;
            continue;
        }
        if (body.transform.position != entity.transform.position ||
            body.transform.rotation != entity.transform.rotation) {
            interface.SetPositionAndRotation(body.id, jvec(entity.transform.position),
                                             jquat(entity.transform.rotation),
                                             JPH::EActivation::Activate);
        }
        if (body.component.linear_velocity != entity.rigid_body->linear_velocity)
            interface.SetLinearVelocity(body.id, jvec(entity.rigid_body->linear_velocity));
        if (body.component.angular_velocity != entity.rigid_body->angular_velocity)
            interface.SetAngularVelocity(body.id, jvec(entity.rigid_body->angular_velocity));
        body.transform = entity.transform;
        body.component = *entity.rigid_body;
    }
    std::erase_if(world.joints, [&](const auto& item) {
        const auto id = item.first;
        bool keep = world.scene.alive(id) && world.scene.get(id).joint && world.bodies.contains(id);
        if (keep) {
            const auto& component = *world.scene.get(id).joint;
            const auto parent = component.parent == 0 || !world.bodies.contains(component.parent)
                                    ? JPH::BodyID()
                                    : world.bodies.at(component.parent).id;
            keep = item.second.component == component &&
                   item.second.child == world.bodies.at(id).id && item.second.parent == parent;
        }
        if (!keep) {
            world.system.RemoveConstraint(item.second.constraint);
            world.joints_changed = true;
        }
        return !keep;
    });
    for (const auto id : world.scene.entities()) {
        const auto& entity = world.scene.get(id);
        if (!entity.joint || world.joints.contains(id))
            continue;
        world.validate_joint(id, entity);
        world.joints.emplace(id, world.create_joint(id, entity));
    }
    if (world.joints_changed)
        world.rebuild_groups();
    world.synced_revision = world.scene.revision();
}

void PhysicsWorld::step(double delta) {
    if (!std::isfinite(delta) || delta <= 0 || delta > 1.0 / 30.0)
        throw std::invalid_argument("physics delta must be in (0, 1/30] seconds");
    sync();
    auto& world = *impl_;
    for (const auto& [id, joint] : world.joints) {
        if (joint.component.damping > 0)
            world.add_joint_force(id, joint,
                                  -joint.component.damping * world.state(joint).velocity);
    }
    const auto result =
        world.system.Update(static_cast<float>(delta), 1, &world.allocator, &world.jobs);
    if (result != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt exceeded its collision capacity");
    world.store_all();
}

PhysicsState PhysicsWorld::save_state() {
    sync();
    PhysicsState state;
    for (const auto& [id, body] : impl_->bodies)
        state.bodies_.emplace_back(id, body.id.GetIndexAndSequenceNumber());
    for (const auto& [id, joint] : impl_->joints)
        state.joints_.emplace_back(id, joint.serial);
    JPH::StateRecorderImpl recorder;
    impl_->system.SaveState(recorder);
    state.data_ = recorder.GetData();
    return state;
}

void PhysicsWorld::restore_state(const PhysicsState& state) {
    if (state.empty())
        throw std::invalid_argument("physics state is empty");
    sync();
    auto& world = *impl_;
    bool same =
        state.bodies_.size() == world.bodies.size() && state.joints_.size() == world.joints.size();
    for (std::size_t i = 0; same && i < state.bodies_.size(); ++i) {
        const auto found = world.bodies.find(state.bodies_[i].first);
        same = found != world.bodies.end() &&
               found->second.id.GetIndexAndSequenceNumber() == state.bodies_[i].second;
    }
    // A rebuilt constraint (a changed joint) no longer matches its saved warm start.
    for (std::size_t i = 0; same && i < state.joints_.size(); ++i) {
        const auto found = world.joints.find(state.joints_[i].first);
        same = found != world.joints.end() && found->second.serial == state.joints_[i].second;
    }
    if (!same)
        throw std::invalid_argument("physics state was saved with other bodies or joints");
    JPH::StateRecorderImpl recorder;
    recorder.WriteBytes(state.data_.data(), state.data_.size());
    if (!world.system.RestoreState(recorder))
        throw std::runtime_error("Jolt could not restore the physics state");
    world.store_all();
}

void PhysicsWorld::apply_impulse(EntityId id, glm::vec3 impulse) {
    if (!std::isfinite(impulse.x) || !std::isfinite(impulse.y) || !std::isfinite(impulse.z) ||
        glm::any(glm::greaterThan(glm::abs(impulse), glm::vec3(100000.0F))))
        throw std::invalid_argument("impulse must be finite and within +/-100000");
    const auto& entity = impl_->scene.get(id);
    if (!entity.rigid_body || entity.rigid_body->motion != "dynamic")
        throw std::invalid_argument("impulses require a dynamic rigid body");
    const auto velocity = entity.rigid_body->linear_velocity + impulse / entity.rigid_body->mass;
    if (glm::length(velocity) > 500.0F)
        throw std::invalid_argument("impulse would exceed the 500 m/s velocity limit");
    sync();
    const auto body = impl_->bodies.at(id).id;
    impl_->bodies_interface().AddImpulse(body, jvec(impulse));
    auto changed = impl_->scene.get(id);
    changed.rigid_body->linear_velocity = gvec(impl_->bodies_interface().GetLinearVelocity(body));
    impl_->scene.set(id, changed);
    impl_->bodies.at(id).component = *changed.rigid_body;
    impl_->synced_revision = impl_->scene.revision();
}

JointState PhysicsWorld::joint_state(EntityId id) {
    sync();
    return impl_->state(impl_->joint_of(id));
}

void PhysicsWorld::set_joint_state(EntityId id, float position, float velocity) {
    if (!std::isfinite(position) || !std::isfinite(velocity))
        throw std::invalid_argument("joint state must be finite");
    sync();
    auto& world = *impl_;
    const auto& joint = world.joint_of(id);
    const auto& component = joint.component;
    if (component.limited && (position < component.limit_min || position > component.limit_max))
        throw std::invalid_argument("joint position is outside its limits");
    const auto current = world.state(joint);
    const float delta_position = position - current.position;
    const float delta_velocity = velocity - current.velocity;
    const auto frame = joint_frame(world.scene.get(id));
    const bool hinge = component.type == "hinge";
    const auto rotation = glm::angleAxis(delta_position, frame.axis);
    const auto& interface = world.bodies_interface();
    // Compute and validate every new pose before touching any body.
    struct Move {
        EntityId id;
        Entity entity;
    };
    std::vector<Move> moves;
    for (const auto member : world.subtree(id)) {
        const auto body = world.bodies.at(member).id;
        auto entity = world.scene.get(member);
        auto p = gvec(interface.GetPosition(body));
        auto q = gquat(interface.GetRotation(body));
        auto v = gvec(interface.GetLinearVelocity(body));
        auto w = gvec(interface.GetAngularVelocity(body));
        if (hinge) {
            p = frame.anchor + rotation * (p - frame.anchor);
            q = glm::normalize(rotation * q);
            w += frame.axis * delta_velocity;
            v += glm::cross(frame.axis * delta_velocity, p - frame.anchor);
        } else {
            p += frame.axis * delta_position;
            v += frame.axis * delta_velocity;
        }
        entity.transform.position = p;
        entity.transform.rotation = q;
        entity.rigid_body->linear_velocity = v;
        entity.rigid_body->angular_velocity = w;
        entity.validate();
        moves.push_back({member, std::move(entity)});
    }
    auto& mutable_interface = world.bodies_interface();
    for (const auto& move : moves) {
        auto& body = world.bodies.at(move.id);
        const auto& t = move.entity.transform;
        const auto& b = *move.entity.rigid_body;
        mutable_interface.SetPositionAndRotation(body.id, jvec(t.position), jquat(t.rotation),
                                                 JPH::EActivation::Activate);
        mutable_interface.SetLinearAndAngularVelocity(body.id, jvec(b.linear_velocity),
                                                      jvec(b.angular_velocity));
        world.store(move.id, body);
    }
    world.synced_revision = world.scene.revision();
}

void PhysicsWorld::apply_joint_force(EntityId id, float value) {
    if (!std::isfinite(value) || std::abs(value) > 100000.0F)
        throw std::invalid_argument("joint force must be finite and within +/-100000");
    sync();
    impl_->add_joint_force(id, impl_->joint_of(id), value);
}

bool PhysicsWorld::touching(EntityId first, EntityId second) {
    sync();
    const auto a = impl_->bodies.at(first).id, b = impl_->bodies.at(second).id;
    if (a == b)
        return false;
    const auto& interface = impl_->bodies_interface();
    const auto shape = interface.GetShape(a);
    const auto transform = interface.GetCenterOfMassTransform(a);
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.02F;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sOne(), transform, settings,
                                                     transform.GetTranslation(), collector, {}, {},
                                                     OnlyBody(b));
    return collector.HadHit();
}

bool PhysicsWorld::grounded(EntityId id) {
    sync();
    const auto body = impl_->bodies.at(id).id;
    JPH::RVec3 center;
    float distance = 0;
    {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), body);
        if (!lock.Succeeded())
            throw std::runtime_error("could not lock ground-query body");
        center = lock.GetBody().GetCenterOfMassPosition();
        distance = center.GetY() - lock.GetBody().GetWorldSpaceBounds().mMin.GetY() + 0.08F;
    }
    const JPH::RRayCast ray(center, JPH::Vec3(0, -distance, 0));
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, GroundFilter(body)))
        return false;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
    return lock.Succeeded() &&
           lock.GetBody()
                   .GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction))
                   .GetY() > 0.6F;
}

std::size_t PhysicsWorld::body_count() const {
    return impl_->bodies.size();
}

std::size_t PhysicsWorld::joint_count() const {
    return impl_->joints.size();
}
} // namespace vsim
