#include <vsim/editor.hpp>
#include <vsim/io.hpp>
#include <vsim/simulation.hpp>

#include <stdexcept>

namespace vsim {
namespace {
constexpr std::size_t history_limit = 32;

void push_history(std::deque<Scene>& history, const Scene& scene) {
    history.push_back(scene);
    if (history.size() > history_limit)
        history.pop_front();
}
} // namespace

Editor::Editor(Scene scene) : scene_(std::move(scene)) {}

void Editor::commit(Scene scene) {
    push_history(undo_, scene_);
    scene_ = std::move(scene);
    redo_.clear();
}

Json Editor::execute(const Json& request) {
    if (!request.is_object())
        throw std::invalid_argument("request must be an object");
    const auto op = request.at("op").get<std::string>();
    if (op == "describe") {
        require_keys(request, {"id", "op"});
        return {{"protocol", 1},
                {"scene_version", 1},
                {"commands",
                 {"describe", "scene", "create", "set", "destroy", "prefab", "spawn", "simulate",
                  "undo", "redo", "load", "save"}},
                {"components", {"name", "transform", "camera", "script", "mesh", "rigid_body"}},
                {"lua_api", ScriptSystem::api_names()},
                {"renderer", false},
                {"physics", true},
                {"physics_backend", "Jolt"},
                {"audio", false}};
    }
    if (op == "scene") {
        require_keys(request, {"id", "op"});
        return scene_.json();
    }
    if (op == "save") {
        require_keys(request, {"id", "op", "path"});
        save_scene(scene_, request.at("path").get<std::string>());
        return {{"saved", true}};
    }
    if (op == "undo" || op == "redo") {
        require_keys(request, {"id", "op"});
        auto& source = op == "undo" ? undo_ : redo_;
        auto& destination = op == "undo" ? redo_ : undo_;
        if (source.empty())
            throw std::runtime_error("history is empty");
        push_history(destination, scene_);
        scene_ = std::move(source.back());
        source.pop_back();
        return {{"changed", true}};
    }
    Scene candidate = scene_;
    Json result = {{"changed", true}};
    if (op == "create") {
        require_keys(request, {"id", "op", "components"});
        result = {{"entity_id", candidate.create(entity_from_json(request.at("components")))}};
    } else if (op == "set") {
        require_keys(request, {"id", "op", "entity_id", "components"});
        candidate.set(entity_id(request.at("entity_id")),
                      entity_from_json(request.at("components")));
    } else if (op == "destroy") {
        require_keys(request, {"id", "op", "entity_id"});
        candidate.destroy(entity_id(request.at("entity_id")));
    } else if (op == "prefab") {
        require_keys(request, {"id", "op", "name", "components"});
        candidate.define_prefab(request.at("name").get<std::string>(),
                                entity_from_json(request.at("components")));
    } else if (op == "spawn") {
        require_keys(request, {"id", "op", "name"});
        result = {{"entity_id", candidate.spawn(request.at("name").get<std::string>())}};
    } else if (op == "load") {
        require_keys(request, {"id", "op", "path"});
        candidate = load_scene(request.at("path").get<std::string>());
    } else if (op == "simulate") {
        require_keys(request, {"id", "op", "ticks", "actions"});
        const auto ticks = entity_id(request.at("ticks"));
        if (ticks > 10000)
            throw std::invalid_argument("simulation is limited to 10000 ticks per command");
        Simulation scripts(candidate);
        const auto frame = input_from_json(request.value("actions", Json::object()));
        for (EntityId i = 0; i < ticks; ++i)
            scripts.tick(1.0 / 60.0, frame);
        result = {{"ticks", ticks}, {"entities", candidate.size()}};
    } else {
        throw std::invalid_argument("unknown operation: " + op);
    }
    // Check publication constraints before creating history or replacing the current scene.
    (void)encode_scene(candidate);
    commit(std::move(candidate));
    return result;
}
} // namespace vsim
