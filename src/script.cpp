#include <vsim/io.hpp>
#include <vsim/physics.hpp>
#include <vsim/script.hpp>

#include <lua.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vsim {
namespace {
static_assert(sizeof(lua_Integer) >= sizeof(EntityId), "VSim requires 64-bit Lua integers");
struct Context {
    Scene* scene;
    const InputFrame* input;
    PhysicsWorld* physics;
    int instructions{0};
    bool closing{false};
    bool in_api{false};
    std::vector<EntityId> result_ids;
    std::string result_json;
};

Context& context(lua_State* state) {
    return **static_cast<Context**>(lua_getextraspace(state));
}

EntityId id_arg(lua_State* state, int index) {
    if (!lua_isinteger(state, index) || lua_tointeger(state, index) <= 0) {
        throw std::invalid_argument("expected a positive integer entity ID");
    }
    return static_cast<EntityId>(lua_tointeger(state, index));
}

std::string string_arg(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TSTRING) {
        throw std::invalid_argument("expected a string");
    }
    std::size_t length = 0;
    const auto* bytes = lua_tolstring(state, index, &length);
    return {bytes, length};
}

float float_arg(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TNUMBER) {
        throw std::invalid_argument("expected a finite number");
    }
    const auto value = lua_tonumber(state, index);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument("number is outside the finite float range");
    }
    return static_cast<float>(value);
}

int create(lua_State* state) {
    Entity entity;
    entity.name = string_arg(state, 1);
    const auto id = context(state).scene->create(std::move(entity));
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int destroy(lua_State* state) {
    context(state).scene->destroy(id_arg(state, 1));
    return 0;
}

int alive(lua_State* state) {
    lua_pushboolean(state, context(state).scene->alive(id_arg(state, 1)));
    return 1;
}

int entities(lua_State* state) {
    auto& ids = context(state).result_ids;
    ids = context(state).scene->entities();
    lua_createtable(state, static_cast<int>(ids.size()), 0);
    lua_Integer index = 1;
    for (const auto id : ids) {
        lua_pushinteger(state, static_cast<lua_Integer>(id));
        lua_rawseti(state, -2, index++);
    }
    return 1;
}

int get(lua_State* state) {
    auto& json = context(state).result_json;
    json = entity_json(context(state).scene->get(id_arg(state, 1))).dump();
    lua_pushlstring(state, json.data(), json.size());
    return 1;
}

int name(lua_State* state) {
    auto& result = context(state).result_json;
    result = context(state).scene->get(id_arg(state, 1)).name;
    lua_pushlstring(state, result.data(), result.size());
    return 1;
}

int input(lua_State* state) {
    const auto action = string_arg(state, 1);
    if (action.empty() || action.size() > 64 || action.find('\0') != std::string::npos) {
        throw std::invalid_argument("action names must contain 1..64 bytes and no NUL");
    }
    const auto found = context(state).input->find(action);
    lua_pushnumber(state, found == context(state).input->end() ? 0.0 : found->second);
    return 1;
}

int set(lua_State* state) {
    const auto id = id_arg(state, 1);
    auto entity = entity_from_json(parse_json(string_arg(state, 2)));
    context(state).scene->set(id, std::move(entity));
    return 0;
}

int position(lua_State* state) {
    const auto p = context(state).scene->get(id_arg(state, 1)).transform.position;
    lua_pushnumber(state, p.x);
    lua_pushnumber(state, p.y);
    lua_pushnumber(state, p.z);
    return 3;
}

int set_position(lua_State* state) {
    const auto id = id_arg(state, 1);
    auto entity = context(state).scene->get(id);
    entity.transform.position = {float_arg(state, 2), float_arg(state, 3), float_arg(state, 4)};
    context(state).scene->set(id, std::move(entity));
    return 0;
}

int set_rotation(lua_State* state) {
    const auto id = id_arg(state, 1);
    auto entity = context(state).scene->get(id);
    entity.transform.rotation = {float_arg(state, 2), float_arg(state, 3), float_arg(state, 4),
                                 float_arg(state, 5)};
    context(state).scene->set(id, std::move(entity));
    return 0;
}

int spawn(lua_State* state) {
    const auto id = context(state).scene->spawn(string_arg(state, 1));
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int define_prefab(lua_State* state) {
    auto name = string_arg(state, 1);
    auto entity = entity_from_json(parse_json(string_arg(state, 2)));
    context(state).scene->define_prefab(std::move(name), std::move(entity));
    return 0;
}

PhysicsWorld& physics(lua_State* state) {
    if (!context(state).physics)
        throw std::runtime_error("physics API requires a Simulation");
    return *context(state).physics;
}

int linear_velocity(lua_State* state) {
    const auto& entity = context(state).scene->get(id_arg(state, 1));
    if (!entity.rigid_body)
        throw std::invalid_argument("entity has no rigid body");
    const auto velocity = entity.rigid_body->linear_velocity;
    lua_pushnumber(state, velocity.x);
    lua_pushnumber(state, velocity.y);
    lua_pushnumber(state, velocity.z);
    return 3;
}

int set_linear_velocity(lua_State* state) {
    const auto id = id_arg(state, 1);
    auto entity = context(state).scene->get(id);
    if (!entity.rigid_body || entity.rigid_body->motion == "static")
        throw std::invalid_argument("velocity requires a dynamic or kinematic rigid body");
    entity.rigid_body->linear_velocity = {float_arg(state, 2), float_arg(state, 3),
                                          float_arg(state, 4)};
    context(state).scene->set(id, std::move(entity));
    return 0;
}

int angular_velocity(lua_State* state) {
    const auto& entity = context(state).scene->get(id_arg(state, 1));
    if (!entity.rigid_body)
        throw std::invalid_argument("entity has no rigid body");
    const auto velocity = entity.rigid_body->angular_velocity;
    lua_pushnumber(state, velocity.x);
    lua_pushnumber(state, velocity.y);
    lua_pushnumber(state, velocity.z);
    return 3;
}

int set_angular_velocity(lua_State* state) {
    const auto id = id_arg(state, 1);
    auto entity = context(state).scene->get(id);
    if (!entity.rigid_body || entity.rigid_body->motion == "static")
        throw std::invalid_argument("velocity requires a dynamic or kinematic rigid body");
    entity.rigid_body->angular_velocity = {float_arg(state, 2), float_arg(state, 3),
                                           float_arg(state, 4)};
    context(state).scene->set(id, std::move(entity));
    return 0;
}

int joint_state(lua_State* state) {
    const auto result = physics(state).joint_state(id_arg(state, 1));
    lua_pushnumber(state, result.position);
    lua_pushnumber(state, result.velocity);
    return 2;
}

int set_joint_state(lua_State* state) {
    physics(state).set_joint_state(id_arg(state, 1), float_arg(state, 2), float_arg(state, 3));
    return 0;
}

int apply_joint_force(lua_State* state) {
    physics(state).apply_joint_force(id_arg(state, 1), float_arg(state, 2));
    return 0;
}

int apply_impulse(lua_State* state) {
    physics(state).apply_impulse(id_arg(state, 1),
                                 {float_arg(state, 2), float_arg(state, 3), float_arg(state, 4)});
    return 0;
}

int touching(lua_State* state) {
    const bool result = physics(state).touching(id_arg(state, 1), id_arg(state, 2));
    lua_pushboolean(state, result);
    return 1;
}

int grounded(lua_State* state) {
    const bool result = physics(state).grounded(id_arg(state, 1));
    lua_pushboolean(state, result);
    return 1;
}

// Lua errors use longjmp. Unwind C++ temporaries and the active exception before raising one.
template <int (*Function)(lua_State*)> int boundary(lua_State* state) {
    std::array<char, 1024> error{};
    const bool previous_in_api = context(state).in_api;
    try {
        if (context(state).closing || context(state).in_api) {
            throw std::runtime_error(
                "engine API is unavailable during shutdown or reentrant calls");
        }
        context(state).in_api = true;
        const auto results = Function(state);
        context(state).in_api = previous_in_api;
        return results;
    } catch (const std::exception& exception) {
        std::snprintf(error.data(), error.size(), "%s", exception.what());
    } catch (...) {
        std::snprintf(error.data(), error.size(), "unknown engine error");
    }
    context(state).in_api = previous_in_api;
    return luaL_error(state, "%s", error.data());
}

const luaL_Reg bindings[] = {{"create", boundary<create>},
                             {"destroy", boundary<destroy>},
                             {"alive", boundary<alive>},
                             {"entities", boundary<entities>},
                             {"get", boundary<get>},
                             {"name", boundary<name>},
                             {"input", boundary<input>},
                             {"set", boundary<set>},
                             {"position", boundary<position>},
                             {"set_position", boundary<set_position>},
                             {"set_rotation", boundary<set_rotation>},
                             {"spawn", boundary<spawn>},
                             {"define_prefab", boundary<define_prefab>},
                             {"linear_velocity", boundary<linear_velocity>},
                             {"set_linear_velocity", boundary<set_linear_velocity>},
                             {"angular_velocity", boundary<angular_velocity>},
                             {"set_angular_velocity", boundary<set_angular_velocity>},
                             {"apply_impulse", boundary<apply_impulse>},
                             {"touching", boundary<touching>},
                             {"grounded", boundary<grounded>},
                             {"joint_state", boundary<joint_state>},
                             {"set_joint_state", boundary<set_joint_state>},
                             {"apply_joint_force", boundary<apply_joint_force>},
                             {nullptr, nullptr}};

void instruction_hook(lua_State* state, lua_Debug*) {
    context(state).instructions -= 1000;
    if (context(state).instructions <= 0) {
        luaL_error(state, "script instruction budget exceeded");
    }
}

int initialize(lua_State* state) {
    const luaL_Reg libraries[] = {{LUA_GNAME, luaopen_base},        {LUA_TABLIBNAME, luaopen_table},
                                  {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_UTF8LIBNAME, luaopen_utf8},  {nullptr, nullptr}};
    for (const auto* library = libraries; library->name != nullptr; ++library) {
        luaL_requiref(state, library->name, library->func, 1);
        lua_pop(state, 1);
    }
    for (const auto* name :
         {"dofile", "loadfile", "load", "print", "warn", "collectgarbage", "pcall", "xpcall"}) {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
    luaL_newlib(state, bindings);
    lua_setglobal(state, "engine");
    return 0;
}

// Fetch and invoke callbacks under pcall so user metatables cannot escape protection.
int invoke(lua_State* state) {
    const auto* name = lua_toboolean(state, 1) ? "on_start" : "on_update";
    lua_getglobal(state, name);
    if (lua_isnil(state, -1)) {
        return 0;
    }
    if (!lua_isfunction(state, -1)) {
        return luaL_error(state, "%s must be a function", name);
    }
    lua_pushvalue(state, 2);
    lua_pushvalue(state, 3);
    lua_call(state, 2, 0);
    return 0;
}

class Instance {
  public:
    Instance(Scene& scene, const InputFrame& frame, PhysicsWorld* physics, EntityId id,
             std::string source)
        : context_{&scene, &frame, physics, 0, false, false, {}, {}}, state_(luaL_newstate()),
          id_(id), source_(std::move(source)) {
        if (!state_) {
            throw std::runtime_error("could not allocate Lua state");
        }
        *static_cast<Context**>(lua_getextraspace(state_.get())) = &context_;
        lua_sethook(state_.get(), instruction_hook, LUA_MASKCOUNT, 1000);
        reset_budget();
        lua_pushcfunction(state_.get(), initialize);
        check(lua_pcall(state_.get(), 0, 0, 0));
        check(luaL_loadbufferx(state_.get(), source_.data(), source_.size(), "behavior", "t"));
        check(lua_pcall(state_.get(), 0, 0, 0));
    }

    void call(bool start, double delta) {
        reset_budget();
        lua_pushcfunction(state_.get(), invoke);
        lua_pushboolean(state_.get(), start);
        lua_pushinteger(state_.get(), static_cast<lua_Integer>(id_));
        lua_pushnumber(state_.get(), delta);
        check(lua_pcall(state_.get(), 3, 0, 0));
    }

    const std::string& source() const noexcept {
        return source_;
    }

  private:
    struct Close {
        void operator()(lua_State* state) const {
            context(state).closing = true;
            lua_close(state);
        }
    };
    void reset_budget() {
        context_.instructions = 100000;
    }
    void check(int status) {
        if (status != LUA_OK) {
            const auto* message = lua_type(state_.get(), -1) == LUA_TSTRING
                                      ? lua_tostring(state_.get(), -1)
                                      : nullptr;
            const std::string error = message ? message : "non-string Lua error";
            lua_pop(state_.get(), 1);
            throw std::runtime_error("script entity " + std::to_string(id_) + ": " + error);
        }
    }
    Context context_;
    std::unique_ptr<lua_State, Close> state_;
    EntityId id_;
    std::string source_;
};
} // namespace

struct ScriptSystem::Impl {
    Scene& scene;
    PhysicsWorld* physics;
    InputFrame input;
    std::map<EntityId, std::unique_ptr<Instance>> instances;
};

ScriptSystem::ScriptSystem(Scene& scene, PhysicsWorld* physics)
    : impl_(std::make_unique<Impl>(Impl{scene, physics, {}, {}})) {}
ScriptSystem::~ScriptSystem() = default;

const std::vector<std::string>& ScriptSystem::api_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> result;
        for (const auto* binding = bindings; binding->name != nullptr; ++binding) {
            result.emplace_back(binding->name);
        }
        return result;
    }();
    return names;
}

void validate_input(const InputFrame& frame) {
    if (frame.size() > 32) {
        throw std::invalid_argument("input frames support at most 32 actions");
    }
    for (const auto& [action, value] : frame) {
        if (action.empty() || action.size() > 64 || action.find('\0') != std::string::npos ||
            !std::isfinite(value) || value < -1.0F || value > 1.0F) {
            throw std::invalid_argument(
                "input requires 1..64-byte action names and finite values in [-1, 1]");
        }
        (void)Json(action).dump();
    }
}

InputFrame input_from_json(const Json& data) {
    if (!data.is_object() || data.size() > 32) {
        throw std::invalid_argument("actions must be an object with at most 32 entries");
    }
    InputFrame frame;
    for (const auto& [action, value] : data.items()) {
        if (!value.is_number()) {
            throw std::invalid_argument("input values must be numbers in [-1, 1]");
        }
        const auto number = value.get<double>();
        if (!std::isfinite(number) || number < -1.0 || number > 1.0) {
            throw std::invalid_argument("input values must be finite numbers in [-1, 1]");
        }
        frame.emplace(action, static_cast<float>(number));
    }
    validate_input(frame);
    return frame;
}

void ScriptSystem::tick(double delta, const InputFrame& frame) {
    if (!std::isfinite(delta) || delta <= 0.0 || delta > 1.0) {
        throw std::invalid_argument("script delta must be in (0, 1] seconds");
    }
    validate_input(frame);
    impl_->input = frame;
    auto& scene = impl_->scene;
    // Snapshot IDs so scripts may create/destroy entities without invalidating iteration.
    // Spawned scripts start on the next tick; destroyed scripts never run later in this tick.
    for (const auto id : scene.entities()) {
        if (!scene.alive(id) || !scene.get(id).script) {
            continue;
        }
        const auto source = scene.get(id).script->source;
        auto& instance = impl_->instances[id];
        if (!instance || instance->source() != source) {
            instance = std::make_unique<Instance>(scene, impl_->input, impl_->physics, id, source);
            if (scene.alive(id) && scene.get(id).script && scene.get(id).script->source == source) {
                instance->call(true, delta);
            }
        }
        if (scene.alive(id) && scene.get(id).script && scene.get(id).script->source == source) {
            instance->call(false, delta);
        }
    }
    std::erase_if(impl_->instances, [&scene](const auto& entry) {
        return !scene.alive(entry.first) || !scene.get(entry.first).script;
    });
}
} // namespace vsim
