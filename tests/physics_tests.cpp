#include <vsim/io.hpp>
#include <vsim/mesh.hpp>
#include <vsim/simulation.hpp>

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
              "engine.grounded(self)"}) {
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
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
