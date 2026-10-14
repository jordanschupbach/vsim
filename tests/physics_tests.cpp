#include <vsim/io.hpp>
#include <vsim/mesh.hpp>
#include <vsim/simulation.hpp>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int passed = 0, failed = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error("check failed: " #condition);                                 \
    } while (false)
void test(const char* name, const std::function<void()>& fn) {
    try {
        fn();
        ++passed;
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
template <typename Fn> void rejects(Fn fn) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}
void near(float a, float b, float tolerance = 0.02F) {
    CHECK(std::abs(a - b) <= tolerance);
}
vsim::Entity body(glm::vec3 position, const char* motion = "dynamic", const char* shape = "box") {
    vsim::Entity entity;
    entity.transform.position = position;
    entity.rigid_body = vsim::RigidBody{};
    entity.rigid_body->motion = motion;
    entity.rigid_body->shape = shape;
    return entity;
}
vsim::EntityId floor(vsim::Scene& scene) {
    auto entity = body({0, -0.5F, 0}, "static");
    entity.transform.scale = {30, 1, 30};
    return scene.create(entity);
}
void step(vsim::Simulation& simulation, int count, const vsim::InputFrame& frame = {}) {
    for (int i = 0; i < count; ++i)
        simulation.tick(1.0 / 60.0, frame);
}
vsim::EntityId named(const vsim::Scene& scene, const std::string& name) {
    for (auto id : scene.entities())
        if (scene.get(id).name == name)
            return id;
    throw std::runtime_error("missing entity: " + name);
}
std::string status(const vsim::Scene& scene) {
    for (auto id : scene.entities())
        if (scene.get(id).name.starts_with("Status: "))
            return scene.get(id).name;
    throw std::runtime_error("missing status");
}
// A hinge pendulum: a capsule arm whose top is pinned to `parent` (0 = world) at
// `anchor`, hanging straight down, confined to the XY plane. Returns the arm.
vsim::EntityId pendulum(vsim::Scene& scene, vsim::EntityId parent, glm::vec3 anchor,
                        float length = 1.2F, float damping = 0) {
    auto arm = body({anchor.x, anchor.y - length / 2, anchor.z}, "dynamic", "capsule");
    arm.transform.scale = {0.2F, length, 0.2F};
    arm.rigid_body->allowed_translation = {true, true, false};
    arm.rigid_body->allowed_rotation = {false, false, true};
    arm.joint = vsim::Joint{};
    arm.joint->parent = parent;
    arm.joint->anchor = {0, length / 2, 0};
    arm.joint->axis = {0, 0, 1};
    arm.joint->damping = damping;
    return scene.create(arm);
}
vsim::Scene game_scene() {
    return vsim::load_scene(std::string(VSIM_SOURCE_DIR) +
                            "/examples/crystal_courier_3d/scene.json");
}
} // namespace

int main() {
    using namespace vsim;
    test("mesh and rigid-body components round trip and can be removed", [] {
        auto entity = entity_from_json({{"mesh", Json::object()}, {"rigid_body", Json::object()}});
        CHECK(entity.mesh && entity.rigid_body);
        CHECK(entity.mesh->primitive == "box" && entity.mesh->roughness == 0.6F);
        CHECK(entity.rigid_body->motion == "static" && entity.rigid_body->mass == 1);
        CHECK(entity_json(entity_from_json(entity_json(entity))) == entity_json(entity));
        Scene scene;
        const auto id = scene.create(entity);
        CHECK(Scene::from_json(scene.json()).json() == scene.json());
        scene.define_prefab("Physical", entity);
        const auto copy = scene.spawn("Physical");
        auto changed = scene.get(copy);
        changed.mesh->color = {1, 0, 0};
        changed.rigid_body->mass = 3;
        scene.set(copy, changed);
        const auto independent = scene.spawn("Physical");
        CHECK(scene.get(independent).mesh->color == entity.mesh->color);
        CHECK(scene.get(independent).rigid_body->mass == 1);
        scene.set(id, Entity{});
        CHECK(!scene.get(id).mesh && !scene.get(id).rigid_body);
    });
    test("material and rigid-body fields are strictly validated", [] {
        for (const Json& invalid :
             {Json{{"mesh", {{"primitive", "unknown"}}}}, Json{{"mesh", {{"roughness", 0}}}},
              Json{{"mesh", {{"metallic", 2}}}}, Json{{"mesh", {{"color", {1, -1, 0}}}}},
              Json{{"mesh", {{"emissive", 11}}}}, Json{{"mesh", {{"unknown", 1}}}},
              Json{{"rigid_body", {{"motion", "flying"}}}},
              Json{{"rigid_body", {{"shape", "unknown"}}}}, Json{{"rigid_body", {{"mass", 0}}}},
              Json{{"rigid_body", {{"friction", -1}}}}, Json{{"rigid_body", {{"restitution", 2}}}},
              Json{{"rigid_body", {{"sensor", 1}}}},
              Json{{"rigid_body", {{"lock_rotation", "yes"}}}},
              Json{{"rigid_body", {{"gravity_scale", -1}}}},
              Json{{"rigid_body", {{"linear_velocity", {1, 0, 0}}}}},
              Json{{"rigid_body", {{"motion", "dynamic"}, {"linear_velocity", {501, 0, 0}}}}},
              Json{{"rigid_body", {{"shape", "sphere"}}}, {"transform", {{"scale", {1, 2, 1}}}}},
              Json{{"rigid_body", Json::object()}, {"transform", {{"scale", {0.001, 1, 1}}}}}})
            rejects([&] { (void)entity_from_json(invalid); });
        Entity entity;
        entity.mesh = Mesh{};
        entity.mesh->roughness = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { entity.validate(); });
    });
    test("primitive triangles have finite unit normals and outward winding", [] {
        for (const auto* primitive : {"box", "sphere", "crystal"}) {
            const auto mesh = primitive_mesh(primitive);
            CHECK(!mesh.empty() && mesh.size() % 3 == 0);
            for (const auto& vertex : mesh) {
                near(glm::length(vertex.normal), 1, 0.0001F);
                CHECK(glm::all(glm::lessThanEqual(glm::abs(vertex.position), glm::vec3(0.50001F))));
            }
            for (std::size_t i = 0; i < mesh.size(); i += 3) {
                const auto a = mesh[i].position, b = mesh[i + 1].position, c = mesh[i + 2].position;
                CHECK(glm::dot(glm::cross(b - a, c - a), a + b + c) > 0);
            }
        }
        CHECK(primitive_mesh("box").size() == 36);
        CHECK(primitive_mesh("crystal").size() == 24);
        rejects([] { (void)primitive_mesh("missing"); });
    });
    test("gravity and collision settle boxes and spheres on the floor", [] {
        for (const auto* shape : {"box", "sphere"}) {
            Scene scene;
            const auto ground = floor(scene);
            const auto id = scene.create(body({0, 4, 0}, "dynamic", shape));
            Simulation simulation(scene);
            step(simulation, 15);
            CHECK(scene.get(id).transform.position.y < 4);
            step(simulation, 225);
            near(scene.get(id).transform.position.y, 0.5F);
            CHECK(simulation.physics().grounded(id));
            CHECK(simulation.physics().touching(id, ground));
            step(simulation, 240);
            CHECK(simulation.physics().grounded(id)); // Sleeping contact must remain queryable.
        }
    });
    test("impulse uses body mass and invalid calls leave the scene intact", [] {
        Scene scene;
        auto entity = body({0, 3, 0});
        entity.rigid_body->mass = 2;
        const auto id = scene.create(entity);
        const auto ground = floor(scene);
        Simulation simulation(scene);
        simulation.physics().apply_impulse(id, {2, 4, 0});
        near(scene.get(id).rigid_body->linear_velocity.x, 1);
        near(scene.get(id).rigid_body->linear_velocity.y, 2);
        const auto before = scene.json();
        rejects([&] { simulation.physics().apply_impulse(ground, {0, 1, 0}); });
        rejects([&] {
            simulation.physics().apply_impulse(id, {std::numeric_limits<float>::infinity(), 0, 0});
        });
        rejects([&] { simulation.physics().apply_impulse(id, {100000, 0, 0}); });
        CHECK(scene.json() == before);
        step(simulation, 1);
        CHECK(scene.get(id).rigid_body->linear_velocity.x < 2);
    });
    test("continuous collision prevents a fast sphere crossing a thin wall", [] {
        Scene scene;
        auto wall = body({2, 0, 0}, "static");
        wall.transform.scale = {0.1F, 10, 10};
        (void)scene.create(wall);
        auto sphere = body({0, 0, 0}, "dynamic", "sphere");
        sphere.rigid_body->linear_velocity = {100, 0, 0};
        sphere.rigid_body->gravity_scale = 0;
        const auto id = scene.create(sphere);
        Simulation simulation(scene);
        step(simulation, 5);
        CHECK(scene.get(id).transform.position.x < 1.6F);
    });
    test("sensor overlaps report contact without supporting a falling body", [] {
        Scene scene;
        auto sensor = body({0, 1, 0}, "static");
        sensor.rigid_body->sensor = true;
        sensor.transform.scale = {5, 1, 5};
        const auto trigger = scene.create(sensor);
        const auto id = scene.create(body({0, 3, 0}, "dynamic", "sphere"));
        Simulation simulation(scene);
        bool touched = false;
        for (int i = 0; i < 90; ++i) {
            step(simulation, 1);
            touched = touched || simulation.physics().touching(id, trigger);
            CHECK(!simulation.physics().grounded(id));
        }
        CHECK(touched && scene.get(id).transform.position.y < 0);
    });
    test("kinematic velocity and static bodies ignore gravity", [] {
        Scene scene;
        auto entity = body({0, 2, 0}, "kinematic");
        entity.rigid_body->linear_velocity = {1, 0, 0};
        const auto moving = scene.create(entity);
        const auto fixed = scene.create(body({0, 6, 0}, "static"));
        Simulation simulation(scene);
        step(simulation, 60);
        near(scene.get(moving).transform.position.x, 1);
        near(scene.get(moving).transform.position.y, 2);
        near(scene.get(fixed).transform.position.y, 6);
    });
    test("dynamic contact transfers motion and restitution produces a bounce", [] {
        Scene scene;
        (void)floor(scene);
        auto moving = body({0, 0.5F, 0}, "dynamic", "sphere");
        moving.rigid_body->linear_velocity = {5, 0, 0};
        moving.rigid_body->mass = 2;
        moving.rigid_body->friction = 0;
        (void)scene.create(moving);
        auto crate = body({2, 0.5F, 0});
        crate.rigid_body->friction = 0;
        const auto pushed = scene.create(crate);
        auto ball = body({-5, 4, 0}, "dynamic", "sphere");
        ball.rigid_body->restitution = 0.8F;
        const auto bouncing = scene.create(ball);
        Simulation simulation(scene);
        bool bounced = false;
        for (int i = 0; i < 120; ++i) {
            step(simulation, 1);
            bounced = bounced || scene.get(bouncing).rigid_body->linear_velocity.y > 3;
        }
        CHECK(scene.get(pushed).transform.position.x > 3);
        CHECK(bounced);
    });
    test("Lua physics API errors unwind safely", [] {
        for (const auto* call :
             {"engine.linear_velocity(self)", "engine.set_linear_velocity(self,1,0,0)",
              "engine.apply_impulse(self,0,1,0)", "engine.touching(self,999)",
              "engine.grounded(self)", "engine.angular_velocity(self)",
              "engine.set_angular_velocity(self,0,1,0)", "engine.joint_state(self)",
              "engine.set_joint_state(self,0,0)", "engine.apply_joint_force(self,1)",
              "engine.joint_state(999)", "engine.set_joint_state(1,0/0,0)",
              "engine.apply_joint_force(1,'x')"}) {
            Scene scene;
            Entity entity;
            entity.script = Script{std::string("function on_update(self) ") + call + " end"};
            (void)scene.create(entity);
            Simulation simulation(scene);
            rejects([&] { step(simulation, 1); });
            CHECK(scene.size() == 1);
        }
    });
    test("teleport resize and component removal reconcile native bodies", [] {
        Scene scene;
        (void)floor(scene);
        const auto id = scene.create(body({0, 1, 0}));
        Simulation simulation(scene);
        step(simulation, 120);
        CHECK(simulation.physics().grounded(id));
        auto changed = scene.get(id);
        changed.transform.position = {0, 4, 0};
        changed.transform.scale = {2, 2, 2};
        changed.transform.rotation = {1.00002F, 0, 0, 0};
        scene.set(id, changed);
        simulation.physics().sync();
        CHECK(!simulation.physics().grounded(id));
        step(simulation, 180);
        near(scene.get(id).transform.position.y, 1);
        near(glm::length(scene.get(id).transform.rotation), 1, 0.000001F);
        changed = scene.get(id);
        changed.rigid_body.reset();
        scene.set(id, changed);
        simulation.physics().sync();
        CHECK(simulation.physics().body_count() == 1);
        rejects([&] { (void)simulation.physics().grounded(id); });
    });
    test("destroyed and spawned bodies do not retain stale native IDs", [] {
        Scene scene;
        scene.define_prefab("Body", body({0, 1, 0}));
        Simulation simulation(scene);
        for (int i = 0; i < 100; ++i) {
            const auto id = scene.spawn("Body");
            step(simulation, 1);
            CHECK(simulation.physics().body_count() == 1);
            scene.destroy(id);
            simulation.physics().sync();
            CHECK(simulation.physics().body_count() == 0);
            rejects([&] { (void)simulation.physics().grounded(id); });
        }
    });
    test("independent physics worlds have independent state", [] {
        Scene a, b;
        const auto id = a.create(body({0, 2, 0}));
        (void)b.create(body({0, 2, 0}));
        Simulation first(a), second(b);
        step(first, 30);
        CHECK(a.get(id).transform.position.y < b.get(id).transform.position.y);
        step(second, 30);
        CHECK(a.json() == b.json());
    });
    test("invalid simulation delta and input do not advance state", [] {
        Scene scene;
        (void)scene.create(body({0, 2, 0}));
        Simulation simulation(scene);
        const auto before = scene.json();
        for (double delta : {0.0, -1.0, 1.0, std::numeric_limits<double>::quiet_NaN()})
            rejects([&] { simulation.tick(delta); });
        rejects([&] { simulation.tick(1.0 / 60, {{"move_x", 2}}); });
        CHECK(scene.json() == before);
    });
    test("Lua-authored prefab spawn velocity and destruction reach Jolt", [] {
        Scene scene;
        Entity controller;
        controller.script = Script{R"(
            local id; local ticks=0
            function on_start(self)
                engine.define_prefab('Ball','{"rigid_body":{"motion":"dynamic","shape":"sphere"},"mesh":{"primitive":"sphere"}}')
                id=engine.spawn('Ball'); engine.set_position(id,0,3,0)
                engine.set_linear_velocity(id,1,0,0); engine.apply_impulse(id,0,1,0)
            end
            function on_update(self) ticks=ticks+1; if ticks==3 then engine.destroy(id) end end
        )"};
        (void)scene.create(controller);
        Simulation simulation(scene);
        step(simulation, 2);
        CHECK(simulation.physics().body_count() == 1);
        CHECK(scene.get(2).transform.position.x > 0);
        step(simulation, 1);
        CHECK(simulation.physics().body_count() == 0 && scene.size() == 1);
    });
    test("example camera orbits continuously with stable aim and retains its components", [] {
        const auto original = game_scene();
        const auto camera = original.get(named(original, "Camera"));
        CHECK(camera.camera && camera.script);
        Scene scene;
        const auto id = scene.create(camera);
        Simulation simulation(scene);
        auto expected = camera;
        glm::vec3 first_position{};
        for (int tick = 1; tick <= 3600; ++tick) {
            simulation.tick(1.0 / 60);
            const auto& current = scene.get(id);
            const auto& transform = current.transform;
            near(glm::length(glm::vec2(transform.position.x, transform.position.z)), 22, 0.0001F);
            near(transform.position.y, 19, 0.0001F);
            near(glm::length(transform.rotation), 1, 0.0001F);
            CHECK(glm::dot(transform.rotation * glm::vec3(0, 0, -1),
                           glm::normalize(-transform.position)) > 0.99999F);
            CHECK((transform.rotation * glm::vec3(0, 1, 0)).y > 0);
            if (tick == 1)
                first_position = transform.position;
            if (tick % 30 == 0) {
                // The whole floor fits both the game window and the narrower
                // default editor viewport, including diagonal orbit angles.
                const auto view = glm::inverse(transform.matrix());
                for (const auto aspect : {1.6F, 720.0F / 678.0F}) {
                    const auto projection = current.camera->projection(aspect);
                    for (const auto x : {-10.0F, 10.0F})
                        for (const auto z : {-8.0F, 8.0F}) {
                            const auto clip = projection * view * glm::vec4(x, 0, z, 1);
                            CHECK(clip.w > 0);
                            CHECK(std::abs(clip.x) < clip.w && std::abs(clip.y) < clip.w);
                            CHECK(clip.z > 0 && clip.z < clip.w);
                        }
                }
            }
            expected.transform = transform;
            CHECK(entity_json(current) == entity_json(expected));
            if (tick == 900) {
                near(transform.position.x, 22, 0.0001F);
                near(transform.position.z, 0, 0.0001F);
            }
        }
        CHECK(glm::length(scene.get(id).transform.position - camera.transform.position) < 0.0001F);
        CHECK(std::abs(glm::dot(scene.get(id).transform.rotation, camera.transform.rotation)) >
              0.99999F);
        simulation.tick(1.0 / 60);
        CHECK(glm::length(scene.get(id).transform.position - first_position) < 0.0001F);
        // New play sessions restart the orbit; equal elapsed time gives equal
        // poses even when the simulation uses different supported time steps.
        Scene first, second;
        const auto a = first.create(camera), b = second.create(camera);
        Simulation normal(first), coarse(second);
        for (int tick = 0; tick < 120; ++tick)
            normal.tick(1.0 / 60);
        for (int tick = 0; tick < 60; ++tick)
            coarse.tick(1.0 / 30);
        CHECK(glm::length(first.get(a).transform.position - second.get(b).transform.position) <
              0.0001F);
        CHECK(std::abs(glm::dot(first.get(a).transform.rotation,
                                second.get(b).transform.rotation)) > 0.99999F);
    });
    test("3D game supports continuous movement grounded jumping and no double jump", [] {
        auto scene = game_scene();
        const auto player = named(scene, "Player");
        Simulation simulation(scene);
        step(simulation, 90);
        CHECK(simulation.physics().grounded(player));
        const auto start = scene.get(player).transform.position;
        step(simulation, 30, {{"move_x", 1}});
        CHECK(scene.get(player).transform.position.x > start.x + 1);
        step(simulation, 1, {{"jump", 1}});
        CHECK(scene.get(player).rigid_body->linear_velocity.y > 5);
        step(simulation, 12, {{"jump", 1}});
        CHECK(scene.get(player).rigid_body->linear_velocity.y < 5);
        step(simulation, 1);
        const auto velocity = scene.get(player).rigid_body->linear_velocity.y;
        step(simulation, 1, {{"jump", 1}});
        CHECK(scene.get(player).rigid_body->linear_velocity.y < velocity);
    });
    test("3D game requires all crystals then wins through collision sensors", [] {
        auto scene = game_scene();
        const auto player = named(scene, "Player");
        const auto exit = named(scene, "Exit");
        Simulation simulation(scene);
        auto visit = [&](EntityId target) {
            auto moved = scene.get(player);
            moved.transform.position = scene.get(target).transform.position;
            moved.rigid_body->linear_velocity = glm::vec3(0);
            scene.set(player, moved);
            step(simulation, 3);
        };
        visit(exit);
        CHECK(status(scene).find("VICTORY") == std::string::npos);
        for (int i = 1; i <= 5; ++i)
            visit(named(scene, "Crystal " + std::to_string(i)));
        CHECK(status(scene).find("5 / 5") != std::string::npos);
        visit(exit);
        CHECK(status(scene).find("VICTORY") != std::string::npos);
        CHECK(scene.get(named(scene, "Trophy")).mesh.has_value());
        const auto count = scene.size();
        step(simulation, 20);
        CHECK(scene.size() == count);
    });
    test("3D game hazard defeat and fresh restart reset progress", [] {
        auto scene = game_scene();
        const auto player = named(scene, "Player");
        Simulation simulation(scene);
        auto moved = scene.get(player);
        moved.transform.position =
            scene.get(named(scene, "Hazard")).transform.position + glm::vec3(0, 0.4F, 0);
        scene.set(player, moved);
        step(simulation, 4);
        CHECK(status(scene).find("Lava!") != std::string::npos);
        auto restarted = game_scene();
        Simulation fresh(restarted);
        step(fresh, 1);
        CHECK(status(restarted).find("0 / 5") != std::string::npos);
    });
    test("3D level can be won using only movement and jump input", [] {
        auto scene = game_scene();
        const auto player = named(scene, "Player");
        Simulation simulation(scene);
        step(simulation, 60);
        auto walk = [&](float x, float z, bool jump = false) {
            for (int tick = 0; tick < 480; ++tick) {
                const auto p = scene.get(player).transform.position;
                if (status(scene).find("VICTORY") != std::string::npos)
                    return;
                if (glm::length(glm::vec2(x - p.x, z - p.z)) < 0.16F) {
                    step(simulation, 12);
                    return;
                }
                step(simulation, 1,
                     {{"move_x", std::clamp((x - p.x) * 2.0F, -1.0F, 1.0F)},
                      {"move_z", std::clamp((z - p.z) * 2.0F, -1.0F, 1.0F)},
                      {"jump", jump && tick % 90 == 0 ? 1.0F : 0.0F}});
            }
            const auto p = scene.get(player).transform.position;
            throw std::runtime_error("unreachable waypoint " + std::to_string(x) + "," +
                                     std::to_string(z) + "; stopped at " + std::to_string(p.x) +
                                     "," + std::to_string(p.y) + "," + std::to_string(p.z));
        };
        walk(-6.5F, 5.5F);
        walk(-7, 0);
        walk(-7, -2);
        walk(-7, -4, true);
        walk(-7, -6.2F);
        walk(0, -5.5F);
        walk(0, -6.2F);
        walk(6.5F, -6.2F);
        walk(6.5F, -5);
        walk(8.5F, -3);
        walk(8.5F, 2);
        walk(6.5F, 2);
        walk(8.7F, 3);
        walk(8.7F, 5.6F);
        walk(7, 5.6F);
        walk(7, 6.5F);
        CHECK(status(scene).find("VICTORY") != std::string::npos);
    });

    test("capsule, axis-lock, angular-velocity, and joint fields round trip and validate", [] {
        const Json full = {{"transform", {{"position", {0, 2, 0}}, {"scale", {0.2, 1, 0.2}}}},
                           {"rigid_body",
                            {{"motion", "dynamic"},
                             {"shape", "capsule"},
                             {"allowed_translation", {true, true, false}},
                             {"allowed_rotation", {false, false, true}},
                             {"angular_velocity", {0, 0, 3}}}},
                           {"joint",
                            {{"type", "hinge"},
                             {"parent", 7},
                             {"anchor", {0, 0.5, 0}},
                             {"axis", {0, 0, 1}},
                             {"limited", true},
                             {"limit_min", -1},
                             {"limit_max", 0.5},
                             {"damping", 2}}}};
        const auto entity = entity_from_json(full);
        CHECK(entity.rigid_body->shape == "capsule" && entity.joint && entity.joint->parent == 7);
        CHECK(entity.rigid_body->allowed_translation == glm::bvec3(true, true, false));
        CHECK(entity.rigid_body->angular_velocity == glm::vec3(0, 0, 3));
        CHECK(entity_json(entity_from_json(entity_json(entity))) == entity_json(entity));
        const auto defaults = entity_from_json({{"rigid_body", Json::object()}});
        CHECK(defaults.rigid_body->allowed_rotation == glm::bvec3(true) &&
              defaults.rigid_body->angular_velocity == glm::vec3(0) && !defaults.joint);
        const Json dynamic = {{"motion", "dynamic"}};
        for (const Json& invalid :
             {Json{{"rigid_body", {{"shape", "capsule"}}}, {"transform", {{"scale", {1, 1, 1}}}}},
              Json{{"rigid_body", {{"shape", "capsule"}}},
                   {"transform", {{"scale", {0.2, 1, 0.3}}}}},
              Json{{"rigid_body", {{"angular_velocity", {0, 0, 1}}}}},
              Json{{"rigid_body", {{"motion", "dynamic"}, {"angular_velocity", {0, 101, 0}}}}},
              Json{{"rigid_body", {{"allowed_translation", {1, 1, 1}}}}},
              Json{{"rigid_body", {{"allowed_rotation", {true, true}}}}},
              Json{{"rigid_body",
                    {{"motion", "dynamic"},
                     {"allowed_translation", {false, false, false}},
                     {"lock_rotation", true}}}},
              Json{{"joint", Json::object()}},
              Json{{"rigid_body", Json::object()}, {"joint", Json::object()}},
              Json{{"rigid_body", dynamic}, {"joint", {{"type", "ball"}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"parent", -1}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"parent", 1.5}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"axis", {0, 0, 0}}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"anchor", {0, 2000, 0}}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"limit_min", 0.1}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"limit_max", -0.1}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"limit_max", 4}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"damping", -1}}}},
              Json{{"rigid_body", dynamic}, {"joint", {{"motor", 1}}}}})
            rejects([&] { (void)entity_from_json(invalid); });
        // Scene files must reference existing, distinct parents.
        Json document = {
            {"version", 1},
            {"entities",
             {{{"id", 1}, {"components", {{"rigid_body", dynamic}, {"joint", {{"parent", 2}}}}}}}}};
        rejects([&] { (void)Scene::from_json(document); });
        document["entities"][0]["components"]["joint"]["parent"] = 1;
        rejects([&] { (void)Scene::from_json(document); });
        document["entities"][0]["components"]["joint"]["parent"] = 0;
        CHECK(Scene::from_json(document).size() == 1);
    });
    test("capsules rest on their caps upright and on their side", [] {
        Scene scene;
        (void)floor(scene);
        auto upright = body({0, 2, 0}, "dynamic", "capsule");
        upright.transform.scale = {0.2F, 1, 0.2F};
        upright.rigid_body->lock_rotation = true;
        const auto standing = scene.create(upright);
        auto sideways = body({3, 2, 0}, "dynamic", "capsule");
        sideways.transform.scale = {0.2F, 1, 0.2F};
        sideways.transform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
        const auto lying = scene.create(sideways);
        Simulation simulation(scene);
        step(simulation, 240);
        near(scene.get(standing).transform.position.y, 0.5F);
        near(scene.get(lying).transform.position.y, 0.1F);
        CHECK(simulation.physics().grounded(standing) && simulation.physics().grounded(lying));
    });
    test("axis locks keep a planar body in its plane", [] {
        Scene scene;
        auto planar = body({0, 5, 0}, "dynamic", "sphere");
        planar.rigid_body->gravity_scale = 0;
        planar.rigid_body->linear_velocity = {1, 0, 3};
        planar.rigid_body->angular_velocity = {2, 2, 2};
        auto free = planar;
        free.transform.position.x = 20; // Well apart: the two spheres must not collide.
        planar.rigid_body->allowed_translation = {true, true, false};
        planar.rigid_body->allowed_rotation = {false, false, true};
        const auto locked = scene.create(planar), loose = scene.create(free);
        Simulation simulation(scene);
        step(simulation, 60);
        near(scene.get(locked).transform.position.x, 0.97F, 0.05F); // Jolt damps slightly.
        near(scene.get(locked).transform.position.z, 0, 0.0001F);
        near(scene.get(locked).rigid_body->angular_velocity.x, 0, 0.0001F);
        near(scene.get(locked).rigid_body->angular_velocity.z, 1.9F, 0.15F);
        CHECK(scene.get(loose).transform.position.z > 2.8F);
    });
    test("hinge angle is right-handed about the axis and the pendulum swings back", [] {
        Scene scene;
        const auto arm = pendulum(scene, 0, {0, 5, 0});
        Simulation simulation(scene);
        auto& physics = simulation.physics();
        CHECK(physics.joint_count() == 1);
        near(physics.joint_state(arm).position, 0, 0.0001F);
        physics.set_joint_state(arm, 0.5F, 0);
        near(physics.joint_state(arm).position, 0.5F, 0.001F);
        near(physics.joint_state(arm).velocity, 0, 0.001F);
        // The arm hangs below the anchor, so a positive angle about +Z moves it to +X.
        CHECK(scene.get(arm).transform.position.x > 0.25F);
        CHECK(scene.get(arm).transform.rotation.z > 0.2F);
        near(glm::length(scene.get(arm).transform.position - glm::vec3(0, 5, 0)), 0.6F, 0.001F);
        float lowest = 1;
        for (int i = 0; i < 90; ++i) {
            step(simulation, 1);
            const auto state = physics.joint_state(arm);
            lowest = std::min(lowest, state.position);
            CHECK(std::abs(state.position) < 0.51F);
        }
        CHECK(lowest < -0.3F); // Swung through the bottom to the other side.
        near(glm::length(scene.get(arm).transform.position - glm::vec3(0, 5, 0)), 0.6F, 0.01F);
        near(scene.get(arm).transform.position.z, 0, 0.001F);
    });
    test("joint limits hold and damping removes energy", [] {
        Scene limited;
        const auto stopped = pendulum(limited, 0, {0, 5, 0});
        auto entity = limited.get(stopped);
        entity.joint->limited = true;
        entity.joint->limit_min = -0.3F;
        entity.joint->limit_max = 0.3F;
        limited.set(stopped, entity);
        Simulation first(limited);
        first.physics().set_joint_state(stopped, 0, 2);
        rejects([&] { first.physics().set_joint_state(stopped, 0.4F, 0); });
        float peak = 0;
        for (int i = 0; i < 120; ++i) {
            step(first, 1);
            peak = std::max(peak, std::abs(first.physics().joint_state(stopped).position));
        }
        CHECK(peak > 0.28F && peak < 0.36F);
        Scene free, damped;
        const auto a = pendulum(free, 0, {0, 5, 0}), b = pendulum(damped, 0, {0, 5, 0}, 1.2F, 3);
        Simulation second(free), third(damped);
        second.physics().set_joint_state(a, 1, 0);
        third.physics().set_joint_state(b, 1, 0);
        float swing_free = 0, swing_damped = 0;
        for (int i = 0; i < 300; ++i) {
            step(second, 1);
            step(third, 1);
            if (i >= 240) {
                swing_free =
                    std::max(swing_free, std::abs(second.physics().joint_state(a).position));
                swing_damped =
                    std::max(swing_damped, std::abs(third.physics().joint_state(b).position));
            }
        }
        CHECK(swing_free > 0.6F && swing_damped < 0.1F); // Jolt's own damping costs a little.
    });
    test("slider moves only along its axis, reports its coordinate, and stops at limits", [] {
        Scene scene;
        auto cart = body({0, 2, 0});
        cart.rigid_body->allowed_rotation = {false, false, false};
        cart.joint = Joint{};
        cart.joint->type = "slider";
        cart.joint->axis = {1, 0, 0};
        cart.joint->limited = true;
        cart.joint->limit_min = -1;
        cart.joint->limit_max = 1;
        const auto id = scene.create(cart);
        Simulation simulation(scene);
        auto& physics = simulation.physics();
        for (int i = 0; i < 30; ++i) {
            physics.apply_joint_force(id, 2); // 2 N on 1 kg: about 0.25 m and 1 m/s after 0.5 s.
            step(simulation, 1);
        }
        const auto state = physics.joint_state(id);
        CHECK(state.position > 0.15F && state.position < 0.35F && state.velocity > 0.7F);
        near(state.position, scene.get(id).transform.position.x, 0.001F);
        near(state.velocity, scene.get(id).rigid_body->linear_velocity.x, 0.001F);
        near(scene.get(id).transform.position.y, 2, 0.001F);
        for (int i = 0; i < 120; ++i) {
            physics.apply_joint_force(id, 20);
            step(simulation, 1);
        }
        near(physics.joint_state(id).position, 1, 0.02F);
        physics.set_joint_state(id, -0.5F, 0);
        near(scene.get(id).transform.position.x, -0.5F, 0.001F);
        rejects([&] { physics.apply_joint_force(id, 1e9F); });
        rejects([&] { physics.apply_joint_force(id, std::numeric_limits<float>::quiet_NaN()); });
        rejects([&] { physics.set_joint_state(id, 0, std::numeric_limits<float>::infinity()); });
        near(scene.get(id).transform.position.x, -0.5F, 0.001F);
    });
    test("joint torque accelerates the child positively and reacts on a dynamic parent", [] {
        Scene scene;
        auto base = body({0, 5, 0});
        base.rigid_body->gravity_scale = 0;
        base.rigid_body->allowed_translation = {true, true, false};
        base.rigid_body->allowed_rotation = {false, false, true};
        const auto parent = scene.create(base);
        const auto arm = pendulum(scene, parent, {0, 4.5F, 0});
        auto weightless = scene.get(arm);
        weightless.rigid_body->gravity_scale = 0;
        scene.set(arm, weightless);
        Simulation simulation(scene);
        auto& physics = simulation.physics();
        for (int i = 0; i < 10; ++i) {
            physics.apply_joint_force(arm, 0.5F);
            step(simulation, 1);
        }
        CHECK(physics.joint_state(arm).velocity > 0.05F);
        CHECK(physics.joint_state(arm).position > 0.001F);
        CHECK(scene.get(parent).rigid_body->angular_velocity.z < -0.001F);
        // Forces are consumed by the step that follows them.
        const auto before = physics.joint_state(arm).velocity;
        step(simulation, 1);
        CHECK(std::abs(physics.joint_state(arm).velocity - before) < 0.01F);
    });
    test("jointed bodies never collide with each other but still collide with others", [] {
        Scene scene;
        const auto parent = scene.create(body({0, 5, 0}, "static"));
        auto arm = body({0, 4.6F, 0}); // Overlaps the static base by 0.4 m.
        arm.rigid_body->gravity_scale = 0;
        arm.joint = Joint{};
        arm.joint->parent = parent;
        arm.joint->anchor = {0, 0.5F, 0};
        const auto child = scene.create(arm);
        const auto dropped = scene.create(body({0, 8, 0}, "dynamic", "sphere"));
        Simulation simulation(scene);
        step(simulation, 120);
        near(scene.get(parent).transform.position.y, 5, 0.001F);
        near(scene.get(child).transform.position.y, 4.6F, 0.001F);
        CHECK(simulation.physics().touching(parent, child));
        near(scene.get(dropped).transform.position.y, 6, 0.05F);
        // Removing the joint makes the overlapping bodies push apart again.
        auto detached = scene.get(child);
        detached.joint.reset();
        scene.set(child, detached);
        step(simulation, 60);
        CHECK(simulation.physics().joint_count() == 0);
        CHECK(scene.get(parent).transform.position.y - scene.get(child).transform.position.y >
              0.9F);
    });
    test("joint references are validated at synchronization and leave scenes intact", [] {
        Scene missing;
        auto orphan = body({0, 1, 0});
        orphan.joint = Joint{};
        orphan.joint->parent = 999;
        (void)missing.create(orphan);
        const auto snapshot = missing.json();
        rejects([&] { Simulation simulation(missing); });
        CHECK(missing.json() == snapshot);
        Scene cycle;
        auto first = body({0, 1, 0}), second = body({0, 2, 0});
        first.joint = Joint{};
        first.joint->parent = 2;
        second.joint = Joint{};
        second.joint->parent = 1;
        (void)cycle.create(first);
        (void)cycle.create(second);
        rejects([&] { Simulation simulation(cycle); });
        Scene unphysical;
        const auto ghost = unphysical.create(Entity{});
        auto hanging = body({0, 1, 0});
        hanging.joint = Joint{};
        hanging.joint->parent = ghost;
        (void)unphysical.create(hanging);
        rejects([&] { Simulation simulation(unphysical); });
        // Losing the parent's body after creation fails the next synchronization.
        Scene scene;
        const auto parent = scene.create(body({0, 5, 0}, "static"));
        const auto arm = pendulum(scene, parent, {0, 5, 0});
        Simulation simulation(scene);
        step(simulation, 1);
        CHECK(simulation.physics().joint_count() == 1);
        scene.destroy(parent);
        rejects([&] { step(simulation, 1); });
        rejects([&] { (void)simulation.physics().joint_state(arm); });
        CHECK(scene.size() == 1);
    });
    test("setting one joint moves the chain below it and keeps the other coordinates", [] {
        Scene scene;
        const auto base = scene.create(body({0, 6, 0}, "static"));
        const auto upper = pendulum(scene, base, {0, 5.5F, 0});
        const auto lower = pendulum(scene, upper, {0, 4.3F, 0});
        Simulation simulation(scene);
        auto& physics = simulation.physics();
        CHECK(physics.joint_count() == 2);
        physics.set_joint_state(upper, 0.7F, 0);
        near(physics.joint_state(upper).position, 0.7F, 0.001F);
        near(physics.joint_state(lower).position, 0, 0.001F);
        CHECK(scene.get(lower).transform.position.x > 0.5F);
        near(glm::length(scene.get(lower).transform.position - glm::vec3(0, 5.5F, 0)), 1.8F,
             0.001F);
        physics.set_joint_state(lower, -0.4F, 0);
        near(physics.joint_state(upper).position, 0.7F, 0.001F);
        near(physics.joint_state(lower).position, -0.4F, 0.001F);
        near(glm::length(scene.get(upper).transform.position - glm::vec3(0, 5.5F, 0)), 0.6F,
             0.001F);
        // Rates: the lower joint rate adds to the upper one's for the lower body.
        physics.set_joint_state(upper, 0.7F, 1);
        physics.set_joint_state(lower, -0.4F, -1);
        near(physics.joint_state(upper).velocity, 1, 0.001F);
        near(physics.joint_state(lower).velocity, -1, 0.001F);
        near(scene.get(upper).rigid_body->angular_velocity.z, 1, 0.001F);
        near(scene.get(lower).rigid_body->angular_velocity.z, 0, 0.001F);
        // Scenes reloaded from JSON rebuild the same joints and coordinates.
        Scene copy = Scene::from_json(scene.json());
        Simulation rebuilt(copy);
        CHECK(rebuilt.physics().joint_count() == 2);
        step(simulation, 30);
        step(rebuilt, 30);
        for (const auto id : scene.entities())
            CHECK(glm::length(scene.get(id).transform.position - copy.get(id).transform.position) <
                  0.01F);
    });
    test("Lua can drive joints and read every body velocity", [] {
        Scene scene;
        const auto arm = pendulum(scene, 0, {0, 5, 0});
        Entity controller;
        controller.script = Script{R"(
            local arm = )" + std::to_string(arm) +
                                   R"(
            function on_start(self)
                engine.set_joint_state(arm, 0.3, 0)
                local angle, rate = engine.joint_state(arm)
                assert(math.abs(angle - 0.3) < 0.001 and math.abs(rate) < 0.001)
                local wx, wy, wz = engine.angular_velocity(arm)
                assert(wx == 0 and wy == 0 and math.abs(wz) < 0.001)
            end
            function on_update(self) engine.apply_joint_force(arm, 40) end
        )"};
        (void)scene.create(controller);
        Simulation simulation(scene);
        step(simulation, 60);
        CHECK(simulation.physics().joint_state(arm).position > 0.3F);
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
