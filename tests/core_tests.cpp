#include <vsim/clock.hpp>
#include <vsim/io.hpp>
#include <vsim/script.hpp>
#include <vsim/simulation.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
int passed = 0;
int failed = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error("check failed: " #condition);                                 \
    } while (false)

template <typename Function> void rejects(Function function) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void near(double actual, double expected, double tolerance = 0.00001) {
    CHECK(std::abs(actual - expected) < tolerance);
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

vsim::Entity behavior(std::string source) {
    vsim::Entity result;
    result.script = vsim::Script{std::move(source)};
    return result;
}

void run_script(const std::string& source) {
    vsim::Scene scene;
    (void)scene.create(behavior(source));
    vsim::ScriptSystem scripts(scene);
    scripts.tick(1.0 / 60.0);
}
} // namespace

int main() {
    using namespace vsim;
    test("Lua input is visible during start/update and clears each tick", [] {
        Scene scene;
        const auto id = scene.create(behavior(
            "assert(engine.input('move_x') == 0.5); "
            "function on_start(self) assert(engine.name(self)=='Entity') end; "
            "function on_update(self) "
            "engine.set_position(self,engine.input('move_x'),engine.input('missing'),0) end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1, {{"move_x", 0.5F}});
        near(scene.get(id).transform.position.x, 0.5);
        scripts.tick(0.1);
        near(scene.get(id).transform.position.x, 0);
    });
    test("input rejects malformed names numbers and collections", [] {
        for (const Json& invalid :
             {Json::array(), Json({{"axis", true}}), Json({{"axis", "1"}}), Json({{"axis", 2}}),
              Json({{"axis", 1e300}}), Json({{"", 0}}), Json({{std::string(65, 'a'), 0}}),
              Json({{std::string("a\0b", 3), 0}})}) {
            rejects([&] { (void)input_from_json(invalid); });
        }
        InputFrame frame;
        for (int i = 0; i < 33; ++i)
            frame[std::to_string(i)] = 0;
        rejects([&] { validate_input(frame); });
        rejects([] { validate_input({{"axis", std::numeric_limits<float>::quiet_NaN()}}); });
        CHECK(input_from_json({{"axis", -1}}).at("axis") == -1);
    });
    test("invalid input does not advance scripts", [] {
        Scene scene;
        const auto id = scene.create(behavior(
            "local n=0; function on_update(self) n=n+1; engine.set_position(self,n,0,0) end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        rejects([&] { scripts.tick(0.1, {{"bad", 2}}); });
        near(scene.get(id).transform.position.x, 1);
        scripts.tick(0.1);
        near(scene.get(id).transform.position.x, 2);
    });
    test("input snapshot is shared by all script instances", [] {
        Scene scene;
        const auto entity = behavior(
            "function on_update(self) engine.set_position(self,engine.input('axis'),0,0) end");
        const auto a = scene.create(entity);
        const auto b = scene.create(entity);
        ScriptSystem scripts(scene);
        scripts.tick(0.1, {{"axis", -1}});
        near(scene.get(a).transform.position.x, -1);
        near(scene.get(b).transform.position.x, -1);
    });
    test("Lua input and name argument errors are contained", [] {
        for (const auto* source :
             {"engine.input(1)", "engine.input('')", "engine.name(999)", "engine.name('1')"}) {
            rejects([&] { run_script(source); });
        }
    });
    test("JSON rejects duplicate keys and excessive nesting", [] {
        rejects([] { (void)parse_json(R"({"name":"a","name":"b"})"); });
        rejects([] { (void)parse_json(std::string(1000, '[') + "0" + std::string(1000, ']')); });
        CHECK(parse_json(R"({"a":{"name":"one"},"b":{"name":"two"}})").size() == 2);
    });
    test("component numbers reject overflow before float conversion", [] {
        rejects([] { (void)entity_from_json({{"transform", {{"position", {1e300, 0, 0}}}}}); });
        rejects([] { (void)entity_from_json({{"camera", {{"far_plane", 1e300}}}}); });
    });
    test("scene text must be valid UTF-8", [] {
        Entity entity;
        entity.name = std::string(1, static_cast<char>(0xff));
        rejects([&] { entity.validate(); });
        entity.name = "valid";
        entity.script = Script{std::string(1, static_cast<char>(0xff))};
        rejects([&] { entity.validate(); });
    });
    test("Lua finalizers cannot mutate scene on shutdown", [] {
        Scene scene;
        (void)scene.create(
            behavior("object=setmetatable({}, {__gc=function() engine.create('unexpected') end})"));
        {
            ScriptSystem scripts(scene);
            scripts.tick(0.1);
        }
        CHECK(scene.size() == 1);
    });
    test("scene encoding enforces the same limit as persistence", [] {
        Scene scene;
        CHECK(Scene::from_json(parse_json(encode_scene(scene))).json() == scene.json());
        Entity entity;
        entity.script = Script{std::string(1024 * 1024, ' ')};
        for (int i = 0; i < 16; ++i)
            (void)scene.create(entity);
        rejects([&] { (void)encode_scene(scene); });
    });
    test("entity lifecycle and stale handles", [] {
        Scene scene;
        const auto id = scene.create();
        CHECK(scene.alive(id));
        scene.destroy(id);
        CHECK(!scene.alive(id));
        const auto next = scene.create();
        CHECK(next > id);
        rejects([&] { (void)scene.get(id); });
        rejects([&] { scene.destroy(id); });
        rejects([&] { scene.set(id, {}); });
    });
    test("stable ordered entity snapshot", [] {
        Scene scene;
        const auto first = scene.create();
        const auto second = scene.create();
        auto ids = scene.entities();
        scene.destroy(first);
        CHECK((ids == std::vector<EntityId>{first, second}));
        CHECK((scene.entities() == std::vector<EntityId>{second}));
    });
    test("component replacement removes optional components", [] {
        Scene scene;
        auto entity = entity_from_json({{"camera", Json::object()}, {"script", {{"source", ""}}}});
        const auto id = scene.create(entity);
        CHECK(scene.get(id).camera && scene.get(id).script);
        scene.set(id, {});
        CHECK(!scene.get(id).camera && !scene.get(id).script);
    });
    test("invalid replacement leaves entity unchanged", [] {
        Scene scene;
        const auto id = scene.create();
        auto invalid = scene.get(id);
        invalid.transform.scale.x = 0;
        rejects([&] { scene.set(id, invalid); });
        CHECK(scene.get(id).transform.scale.x == 1);
    });
    test("invalid creation does not consume an ID", [] {
        Scene scene;
        Entity invalid;
        invalid.name = "";
        rejects([&] { (void)scene.create(invalid); });
        CHECK(scene.create() == 1);
    });
    test("names reject empty long and embedded NUL", [] {
        for (auto name : {std::string(), std::string(129, 'x'), std::string("a\0b", 3)}) {
            Entity entity;
            entity.name = name;
            rejects([&] { entity.validate(); });
        }
    });
    test("prefab instances are independent", [] {
        Scene scene;
        scene.define_prefab("box", {});
        const auto first = scene.spawn("box");
        auto edited = scene.get(first);
        edited.transform.position.x = 7;
        scene.set(first, edited);
        const auto second = scene.spawn("box");
        near(scene.get(second).transform.position.x, 0);
        rejects([&] { (void)scene.spawn("missing"); });
    });
    test("transform TRS composition", [] {
        Transform transform;
        transform.position = {1, 2, 3};
        transform.scale = {2, 3, 4};
        const auto point = transform.matrix() * glm::vec4(1, 1, 1, 1);
        near(point.x, 3);
        near(point.y, 5);
        near(point.z, 7);
    });
    test("quaternion rotation", [] {
        Transform transform;
        transform.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 1, 0));
        const auto point = transform.matrix() * glm::vec4(1, 0, 0, 1);
        near(point.x, 0);
        near(point.z, -1);
    });
    test("transform rejects nonfinite and singular values", [] {
        for (float bad : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
            Transform transform;
            transform.scale.y = bad;
            rejects([&] { transform.validate(); });
        }
        Transform transform;
        transform.position.x = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { transform.validate(); });
    });
    test("transform rejects non-unit quaternion", [] {
        Transform transform;
        transform.rotation = {0, 0, 0, 0};
        rejects([&] { transform.validate(); });
    });
    test("camera Vulkan depth range and Y convention", [] {
        Camera camera;
        const auto projection = camera.projection(16.0F / 9.0F);
        auto point = projection * glm::vec4(0, 0, -camera.near_plane, 1);
        near(point.z / point.w, 0);
        point = projection * glm::vec4(0, 0, -camera.far_plane, 1);
        near(point.z / point.w, 1);
        CHECK(projection[1][1] < 0);
    });
    test("camera invalid frusta", [] {
        Camera camera;
        camera.far_plane = camera.near_plane;
        rejects([&] { camera.validate(); });
        camera = {};
        camera.vertical_fov = 180;
        rejects([&] { camera.validate(); });
        camera = {};
        rejects([&] { (void)camera.projection(0); });
        rejects([&] { (void)camera.projection(std::numeric_limits<float>::infinity()); });
    });
    test("camera avoids intermediate overflow and rejects unrepresentable projection", [] {
        Camera camera;
        camera.near_plane = 1e20F;
        camera.far_plane = 1e30F;
        const auto projection = camera.projection(1.0F);
        CHECK(std::isfinite(projection[3][2]));
        CHECK(projection[3][2] < 0);
        rejects([&] { (void)camera.projection(std::numeric_limits<float>::denorm_min()); });
    });
    test("scene roundtrip retains deleted ID high watermark", [] {
        Scene scene;
        const auto id = scene.create();
        scene.destroy(id);
        auto loaded = Scene::from_json(scene.json());
        CHECK(loaded.json() == scene.json());
        CHECK(loaded.create() > id);
    });
    test("full component and prefab roundtrip", [] {
        Scene scene;
        auto entity = entity_from_json({{"name", "Camera"},
                                        {"camera", Json::object()},
                                        {"script", {{"source", "function on_update() end"}}}});
        (void)scene.create(entity);
        scene.define_prefab("camera", entity);
        auto loaded = Scene::from_json(Json::parse(scene.json().dump()));
        CHECK(loaded.json() == scene.json());
        CHECK(loaded.get(loaded.spawn("camera")).camera.has_value());
    });
    test("schema rejects unknown fields at every level", [] {
        rejects([] { (void)entity_from_json({{"mesh", "unimplemented"}}); });
        rejects([] { (void)entity_from_json({{"transform", {{"postion", {0, 0, 0}}}}}); });
        rejects([] { (void)entity_from_json({{"camera", {{"fov", 60}}}}); });
        rejects([] { (void)entity_from_json({{"script", {{"path", "x.lua"}}}}); });
        auto data = Scene{}.json();
        data["unknown"] = 1;
        rejects([&] { (void)Scene::from_json(data); });
    });
    test("schema rejects unsupported or fractional version", [] {
        auto data = Scene{}.json();
        for (const Json& version : {Json(2), Json(1.0), Json("1"), Json(true)}) {
            data["version"] = version;
            rejects([&] { (void)Scene::from_json(data); });
        }
    });
    test("schema rejects duplicate IDs", [] {
        Scene scene;
        (void)scene.create();
        auto data = scene.json();
        data["entities"].push_back(data["entities"][0]);
        rejects([&] { (void)Scene::from_json(data); });
    });
    test("ID parsing rejects coercion overflow and invalid values", [] {
        for (const Json& id : {Json(-1), Json(0), Json(1.1), Json("1"), Json(true),
                               Json(std::numeric_limits<std::uint64_t>::max())}) {
            rejects([&] { (void)entity_id(id); });
        }
    });
    test("schema rejects regressing next ID", [] {
        Scene scene;
        (void)scene.create();
        auto data = scene.json();
        data["next_id"] = 1;
        rejects([&] { (void)Scene::from_json(data); });
    });
    test("schema rejects malformed vectors", [] {
        for (const Json& vector : {Json::array({1, 2}), Json::array({1, "x", 3}),
                                   Json::array({1, true, 3}), Json::object()}) {
            rejects([&] { (void)entity_from_json({{"transform", {{"position", vector}}}}); });
        }
    });
    test("entity capacity limit", [] {
        Scene scene;
        for (std::size_t i = 0; i < Scene::max_entities; ++i)
            (void)scene.create();
        rejects([&] { (void)scene.create(); });
        CHECK(scene.size() == Scene::max_entities);
    });
    test("clock fractional accumulation", [] {
        FixedClock clock(0.01);
        auto batch = clock.advance(0.004);
        CHECK(batch.steps == 0);
        near(batch.alpha, 0.4);
        batch = clock.advance(0.011);
        CHECK(batch.steps == 1);
        near(batch.alpha, 0.5);
        near(batch.dropped_seconds, 0);
    });
    test("clock overload bounded and reported", [] {
        FixedClock clock(0.01, 4);
        const auto batch = clock.advance(1000);
        CHECK(batch.steps == 4);
        near(batch.dropped_seconds, 999.96);
        CHECK(batch.alpha >= 0 && batch.alpha < 1);
    });
    test("clock invalid input preserves remainder", [] {
        FixedClock clock(0.01);
        (void)clock.advance(0.005);
        for (double bad : {-1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
            rejects([&] { (void)clock.advance(bad); });
        }
        near(clock.advance(0).alpha, 0.5);
        rejects([] { FixedClock invalid(0); });
        rejects([] { FixedClock invalid(0.01, 0); });
    });
    test("clock long-run conservation", [] {
        FixedClock clock;
        std::size_t steps = 0;
        TickBatch batch{};
        for (int i = 0; i < 10000; ++i) {
            batch = clock.advance(0.003);
            steps += batch.steps;
        }
        near((static_cast<double>(steps) + batch.alpha) * clock.step(), 30.0, 1e-9);
    });
    test("Lua API conformance scene", [] {
        auto scene = load_scene(std::string(VSIM_SOURCE_DIR) + "/scenes/conformance.json");
        Simulation scripts(scene);
        for (int i = 0; i < 120; ++i)
            scripts.tick(1.0 / 60.0);
        near(scene.get(1).transform.position.x, 2.0);
        CHECK(scene.size() == 3);
        CHECK(scene.get(5).camera.has_value());
    });
    test("Lua start once and persistent local state", [] {
        Scene scene;
        const auto id = scene.create(
            behavior("local n=0; function on_start(self) engine.create('once') end; function "
                     "on_update(self,dt) n=n+1; engine.set_position(self,n,0,0) end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        scripts.tick(0.1);
        CHECK(scene.size() == 2);
        near(scene.get(id).transform.position.x, 2);
    });
    test("Lua isolated per-entity globals", [] {
        Scene scene;
        const auto entity =
            behavior("n=0; function on_update(self) n=n+1; engine.set_position(self,n,0,0) end");
        const auto a = scene.create(entity);
        const auto b = scene.create(entity);
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        near(scene.get(a).transform.position.x, 1);
        near(scene.get(b).transform.position.x, 1);
    });
    test("Lua self destruction in start skips update", [] {
        Scene scene;
        (void)scene.create(behavior("function on_start(self) engine.destroy(self) end; function "
                                    "on_update() error('must not run') end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        scripts.tick(0.1);
        CHECK(scene.size() == 0);
    });
    test("Lua destruction of later entity skips its script", [] {
        Scene scene;
        (void)scene.create(behavior("function on_start() engine.destroy(2) end"));
        (void)scene.create(behavior("error('must not load')"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        CHECK(scene.size() == 1);
    });
    test("Lua spawned script starts next tick", [] {
        Scene scene;
        scene.define_prefab(
            "child", behavior("function on_start(self) engine.set_position(self,9,0,0) end"));
        (void)scene.create(behavior("function on_start() engine.spawn('child') end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        near(scene.get(2).transform.position.x, 0);
        scripts.tick(0.1);
        near(scene.get(2).transform.position.x, 9);
    });
    test("Lua changed source restarts instance", [] {
        Scene scene;
        const auto id =
            scene.create(behavior("function on_start(self) engine.set_position(self,1,0,0) end"));
        ScriptSystem scripts(scene);
        scripts.tick(0.1);
        scene.set(id, behavior("function on_start(self) engine.set_position(self,2,0,0) end"));
        scripts.tick(0.1);
        near(scene.get(id).transform.position.x, 2);
    });
    test("Lua quaternion setter preserves camera script and other transform fields", [] {
        Scene scene;
        auto entity = behavior("function on_update(self) engine.set_rotation(self,0,0,1,0) end");
        entity.name = "Orbit camera";
        entity.camera = Camera{48, 0.1F, 150};
        entity.transform.position = {1, 2, 3};
        entity.transform.scale = {2, 3, 4};
        const auto id = scene.create(entity);
        ScriptSystem scripts(scene);
        scripts.tick(1.0 / 60);
        entity.transform.rotation = {0, 0, 1, 0};
        CHECK(entity_json(scene.get(id)) == entity_json(entity));
        CHECK(scene.get(id).transform.rotation * glm::vec3(0, 0, -1) == glm::vec3(0, 0, 1));
    });
    test("Lua invalid quaternion setters reject without changing the entity", [] {
        for (const auto* arguments :
             {"self,0,0,0,0", "self,2,0,0,0", "self,0/0,0,0,1", "self,1,math.huge,0,0",
              "self,1,0,0,'0'", "self,1,0,0", "999,1,0,0,0", "0,1,0,0,0", "1.5,1,0,0,0"}) {
            Scene scene;
            const auto id = scene.create(behavior(std::string("function on_update(self) ") +
                                                  "engine.set_rotation(" + arguments + ") end"));
            const auto before = scene.json();
            ScriptSystem scripts(scene);
            rejects([&] { scripts.tick(1.0 / 60); });
            CHECK(scene.json() == before);
            CHECK(scene.alive(id));
        }
    });
    test("Lua syntax and callback failures are reported", [] {
        rejects([] { run_script("this is invalid lua!"); });
        rejects([] { run_script("function on_update() error('failure') end"); });
        rejects([] { run_script("on_update = 1"); });
        rejects([] { run_script("error({})"); });
    });
    test("Lua invalid engine calls are contained", [] {
        for (const auto* source :
             {"engine.destroy(999)", "engine.create(1)", "engine.get(-1)", "engine.get(1.5)",
              "engine.spawn('missing')", "engine.set(1, '{} nope')",
              "engine.set_position(1, 0/0, 0, 0)", "engine.set_position(1, '2', 0, 0)"}) {
            rejects([&] { run_script(source); });
        }
    });
    test("Lua runaway loops hit instruction budget", [] {
        rejects([] { run_script("while true do end"); });
        rejects([] { run_script("function on_update() while true do end end"); });
    });
    test("Lua host libraries and catchable budget escapes unavailable", [] {
        run_script("assert(os==nil and io==nil and package==nil and debug==nil and coroutine==nil "
                   "and pcall==nil and xpcall==nil and load==nil)");
    });
    test("Lua invalid delta rejected before executing", [] {
        Scene scene;
        (void)scene.create(behavior("error('must not execute')"));
        ScriptSystem scripts(scene);
        rejects([&] { scripts.tick(0); });
        rejects([&] { scripts.tick(-1); });
        rejects([&] { scripts.tick(std::numeric_limits<double>::quiet_NaN()); });
    });
    test("bounded protocol reader recovers after oversized line", [] {
        std::istringstream stream("12345\nok\nlast");
        std::string line;
        rejects([&] { (void)read_bounded_line(stream, line, 4); });
        CHECK(read_bounded_line(stream, line, 4) && line == "ok");
        CHECK(read_bounded_line(stream, line, 4) && line == "last");
        CHECK(!read_bounded_line(stream, line, 4));
    });
    test("missing scene rejected",
         [] { rejects([] { (void)load_scene("/nonexistent-vsim-scene.json"); }); });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
