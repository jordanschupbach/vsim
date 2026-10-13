#include <Jolt/Jolt.h> // Jolt requires its platform definitions before all other Jolt headers.

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/SimShapeFilter.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <vsim/physics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

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
    return a == b;
}
} // namespace

struct PhysicsWorld::Impl {
    struct Body {
        JPH::BodyID id;
        Transform transform;
        RigidBody component;
    };
    Scene& scene;
    Layers layers;
    JPH::SimShapeFilter shape_filter;
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImplWithMallocFallback allocator{8 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    std::map<EntityId, Body> bodies;

    explicit Impl(Scene& source) : scene(source) {
        system.Init(static_cast<JPH::uint>(Scene::max_entities + 1), 0, 65536, 20000, layers,
                    layers, layers);
        system.SetGravity(JPH::Vec3(0, -9.81F, 0));
        // Use Jolt's identity filter. In 5.3, the null-filter fast path aliases a
        // ShapeFilter as SimShapeFilterWrapper, which fails UBSan vptr checks.
        system.SetSimShapeFilter(&shape_filter);
    }
    ~Impl() {
        for (const auto& [id, body] : bodies) {
            (void)id;
            system.GetBodyInterface().RemoveBody(body.id);
            system.GetBodyInterface().DestroyBody(body.id);
        }
    }
    Body create(EntityId id, const Entity& entity) {
        const auto& component = *entity.rigid_body;
        const auto& transform = entity.transform;
        JPH::RefConst<JPH::Shape> shape;
        if (component.shape == "sphere") {
            shape = new JPH::SphereShape(transform.scale.x * 0.5F);
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
        settings.mMaxLinearVelocity = 500.0F;
        settings.mMotionQuality = motion == JPH::EMotionType::Dynamic && !component.sensor
                                      ? JPH::EMotionQuality::LinearCast
                                      : JPH::EMotionQuality::Discrete;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = component.mass;
        if (component.lock_rotation)
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                                    JPH::EAllowedDOFs::TranslationY |
                                    JPH::EAllowedDOFs::TranslationZ;
        auto& interface = system.GetBodyInterface();
        const auto body = interface.CreateAndAddBody(settings, motion == JPH::EMotionType::Static
                                                                   ? JPH::EActivation::DontActivate
                                                                   : JPH::EActivation::Activate);
        if (body.IsInvalid())
            throw std::runtime_error("Jolt body capacity exhausted");
        return {body, transform, component};
    }
    void remove(JPH::BodyID body) {
        system.GetBodyInterface().RemoveBody(body);
        system.GetBodyInterface().DestroyBody(body);
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
    auto& interface = world.system.GetBodyInterface();
    std::erase_if(world.bodies, [&](const auto& item) {
        if (world.scene.alive(item.first) && world.scene.get(item.first).rigid_body)
            return false;
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
        body.transform = entity.transform;
        body.component = *entity.rigid_body;
    }
}

void PhysicsWorld::step(double delta) {
    if (!std::isfinite(delta) || delta <= 0 || delta > 1.0 / 30.0)
        throw std::invalid_argument("physics delta must be in (0, 1/30] seconds");
    sync();
    auto& world = *impl_;
    const auto result =
        world.system.Update(static_cast<float>(delta), 1, &world.allocator, &world.jobs);
    if (result != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Jolt exceeded its collision capacity");
    const auto& interface = world.system.GetBodyInterface();
    for (auto& [id, body] : world.bodies) {
        auto entity = world.scene.get(id);
        entity.transform.position = gvec(interface.GetPosition(body.id));
        const auto rotation = interface.GetRotation(body.id);
        entity.transform.rotation = {rotation.GetW(), rotation.GetX(), rotation.GetY(),
                                     rotation.GetZ()};
        entity.rigid_body->linear_velocity = gvec(interface.GetLinearVelocity(body.id));
        world.scene.set(id, entity);
        body.transform = entity.transform;
        body.component = *entity.rigid_body;
    }
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
    impl_->system.GetBodyInterface().AddImpulse(body, jvec(impulse));
    auto changed = impl_->scene.get(id);
    changed.rigid_body->linear_velocity =
        gvec(impl_->system.GetBodyInterface().GetLinearVelocity(body));
    impl_->scene.set(id, changed);
    impl_->bodies.at(id).component = *changed.rigid_body;
}

bool PhysicsWorld::touching(EntityId first, EntityId second) {
    sync();
    const auto a = impl_->bodies.at(first).id, b = impl_->bodies.at(second).id;
    if (a == b)
        return false;
    const auto& interface = impl_->system.GetBodyInterface();
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
} // namespace vsim
