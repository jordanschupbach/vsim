#include <vsim/editor.hpp>
#include <vsim/editor_geometry.hpp>

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
template <typename Fn> void rejects(Fn function) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}
void test(const char* name, const std::function<void()>& function) {
    try {
        function();
        ++passed;
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
vsim::Entity mesh(const char* primitive, glm::vec3 position) {
    vsim::Entity entity;
    entity.mesh = vsim::Mesh{};
    entity.mesh->primitive = primitive;
    entity.transform.position = position;
    return entity;
}
} // namespace
int main() {
    using namespace vsim;
    Transform camera;
    camera.position = {0, 0, 5};
    test("picking intersects every primitive and ignores invisible entities", [&] {
        for (const auto* primitive : {"box", "sphere", "crystal"}) {
            Scene scene;
            Entity invisible;
            invisible.transform.position.z = 3;
            (void)scene.create(invisible);
            const auto id = scene.create(mesh(primitive, {}));
            CHECK(pick_entity(scene, camera, {}, 1, {0.5F, 0.5F}) == id);
            CHECK(!pick_entity(scene, camera, {}, 1, {0.05F, 0.05F}));
        }
    });
    test("picking chooses nearest geometry independent of entity order", [&] {
        Scene scene;
        const auto far = scene.create(mesh("box", {0, 0, -2}));
        const auto near = scene.create(mesh("sphere", {0, 0, 2}));
        CHECK(pick_entity(scene, camera, {}, 1, {0.5F, 0.5F}) == near);
        scene.destroy(near);
        CHECK(pick_entity(scene, camera, {}, 1, {0.5F, 0.5F}) == far);
    });
    test("picking respects clip planes and viewport boundaries", [&] {
        Scene scene;
        (void)scene.create(mesh("box", {0, 0, 10}));
        (void)scene.create(mesh("sphere", {0, 0, -20}));
        Camera lens;
        lens.far_plane = 10;
        CHECK(!pick_entity(scene, camera, lens, 1, {0.5F, 0.5F}));
        CHECK(!pick_entity(scene, camera, lens, 1, {-0.1F, 0.5F}));
        rejects([&] { (void)pick_entity(scene, camera, lens, 0, {0.5F, 0.5F}); });
        rejects([&] {
            (void)pick_entity(scene, camera, lens, 1, {std::numeric_limits<float>::quiet_NaN(), 0});
        });
    });
    test("picking supports large far-near ratios without singular projection inversion", [&] {
        Scene scene;
        const auto id = scene.create(mesh("box", {}));
        Camera lens;
        lens.far_plane = 1e20F;
        lens.near_plane = 0.001F;
        CHECK(pick_entity(scene, camera, lens, 1, {0.5F, 0.5F}) == id);
    });
    test("picking accounts for rotated nonuniform scale and screen Y orientation", [&] {
        Scene scene;
        auto entity = mesh("box", {0, 1, 0});
        entity.transform.scale = {2, 0.3F, 0.5F};
        entity.transform.rotation = glm::angleAxis(0.3F, glm::vec3(0, 0, 1));
        const auto id = scene.create(entity);
        Camera lens;
        const auto clip = lens.projection(1) * glm::vec4(0, 1, -5, 1);
        const glm::vec2 screen = glm::vec2(clip) / clip.w * 0.5F + 0.5F;
        CHECK(screen.y < 0.5F);
        CHECK(pick_entity(scene, camera, lens, 1, screen) == id);
        CHECK(!pick_entity(scene, camera, lens, 1, {screen.x, 1 - screen.y}));
    });
    test("editor public views preserve authoring through play destruction and restart", [] {
        Scene scene;
        auto entity = mesh("box", {0, 5, 0});
        entity.rigid_body = RigidBody{};
        entity.rigid_body->motion = "dynamic";
        const auto id = scene.create(entity);
        Editor editor(scene);
        CHECK(!editor.playing());
        CHECK(!editor.dirty());
        (void)editor.execute({{"op", "select"}, {"entity_id", id}});
        (void)editor.execute({{"op", "play"}});
        (void)editor.execute({{"op", "tick"}, {"ticks", 30}});
        CHECK(editor.displayed_scene().get(id).transform.position.y < 4);
        CHECK(editor.scene().get(id).transform.position.y == 5);
        CHECK(editor.selection() == id);
        (void)editor.execute({{"op", "stop"}});
        CHECK(editor.displayed_scene().get(id).transform.position.y == 5);
        (void)editor.execute({{"op", "play"}});
        CHECK(editor.displayed_scene().get(id).transform.position.y == 5);
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
