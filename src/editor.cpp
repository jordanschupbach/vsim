#include <vsim/editor.hpp>
#include <vsim/editor_geometry.hpp>
#include <vsim/io.hpp>
#include <vsim/simulation.hpp>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace vsim {
namespace {
constexpr std::size_t history_limit = 32;

void push_history(std::deque<Scene>& history, const Scene& scene) {
    history.push_back(scene);
    if (history.size() > history_limit)
        history.pop_front();
}

EntityId tick_count(const Json& request) {
    const auto ticks = entity_id(request.at("ticks"));
    if (ticks > 10000)
        throw std::invalid_argument("simulation is limited to 10000 ticks per command");
    return ticks;
}

float viewport_aspect(const Json& request) {
    const auto value = request.value("aspect", Json(1.0));
    if (!value.is_number())
        throw std::invalid_argument("viewport aspect must be a number");
    const auto aspect = value.get<float>();
    if (!std::isfinite(aspect) || aspect <= 0)
        throw std::invalid_argument("viewport aspect must be finite and positive");
    return aspect;
}

EditorView scene_view(const Scene& scene) {
    EditorView view;
    view.transform.position = {12, 10, 16};
    view.transform.rotation = glm::quat_cast(
        glm::inverse(glm::lookAtRH(view.transform.position, glm::vec3(0), glm::vec3(0, 1, 0))));
    for (const auto id : scene.entities()) {
        const auto& entity = scene.get(id);
        if (entity.camera) {
            view.transform = entity.transform;
            view.camera = *entity.camera;
            break;
        }
    }
    return view;
}

// Only scene mutations belong in a batch. File I/O, history and play sessions
// cannot be rolled back by copying a Scene, so they are deliberately excluded.
Json mutate(Scene& candidate, const Json& request) {
    if (!request.is_object())
        throw std::invalid_argument("request must be an object");
    const auto op = request.at("op").get<std::string>();
    if (op == "create") {
        require_keys(request, {"id", "op", "components"});
        return {{"entity_id", candidate.create(entity_from_json(request.at("components")))}};
    }
    if (op == "set") {
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
        return {{"entity_id", candidate.spawn(request.at("name").get<std::string>())}};
    } else if (op == "simulate") {
        require_keys(request, {"id", "op", "ticks", "actions"});
        const auto ticks = tick_count(request);
        const auto frame = input_from_json(request.value("actions", Json::object()));
        Simulation simulation(candidate);
        for (EntityId i = 0; i < ticks; ++i)
            simulation.tick(1.0 / 60.0, frame);
        return {{"ticks", ticks}, {"entities", candidate.size()}};
    } else {
        throw std::invalid_argument("unknown scene operation: " + op);
    }
    return {{"changed", true}};
}
} // namespace

struct Editor::PlayState {
    Scene scene;
    Simulation simulation;
    explicit PlayState(const Scene& source) : scene(source), simulation(scene) {}
};

Json EditorView::json() const {
    Entity entity;
    entity.transform = transform;
    entity.camera = camera;
    auto result = entity_json(entity);
    result.erase("name");
    return result;
}

Editor::Editor(Scene scene, std::filesystem::path document_path)
    : scene_(std::move(scene)), view_(scene_view(scene_)), saved_document_(encode_scene(scene_)),
      document_path_(std::move(document_path)) {}
Editor::~Editor() = default;
const Scene& Editor::scene() const noexcept {
    return scene_;
}
const Scene& Editor::displayed_scene() const noexcept {
    return play_ ? play_->scene : scene_;
}
const EditorView& Editor::view() const noexcept {
    return view_;
}
bool Editor::playing() const noexcept {
    return play_ != nullptr;
}
bool Editor::dirty() const noexcept {
    return dirty_;
}
std::uint64_t Editor::revision() const noexcept {
    return revision_;
}
const std::filesystem::path& Editor::document_path() const noexcept {
    return document_path_;
}
std::optional<EntityId> Editor::selection() const noexcept {
    return selection_;
}

void Editor::commit(Scene scene) {
    const auto dirty = encode_scene(scene) != saved_document_;
    push_history(undo_, scene_);
    scene_ = std::move(scene);
    ++revision_;
    dirty_ = dirty;
    redo_.clear();
    if (selection_ && !scene_.alive(*selection_))
        selection_.reset();
}

Json Editor::execute(const Json& request) {
    if (!request.is_object())
        throw std::invalid_argument("request must be an object");
    const auto op = request.at("op").get<std::string>();
    if (op == "describe") {
        require_keys(request, {"id", "op"});
        return {{"protocol", 1},
                {"scene_version", 1},
                {"commands", {"describe",   "scene",    "create",   "set",   "destroy", "prefab",
                              "spawn",      "simulate", "undo",     "redo",  "load",    "save",
                              "batch",      "status",   "select",   "play",  "tick",    "stop",
                              "play_scene", "view",     "set_view", "focus", "pick"}},
                {"components",
                 {"name", "transform", "camera", "script", "mesh", "rigid_body", "joint"}},
                {"lua_api", ScriptSystem::api_names()},
                {"renderer", false},
                {"physics", true},
                {"physics_backend", "Jolt"},
                {"audio", false}};
    }
    if (op == "scene" || op == "play_scene") {
        require_keys(request, {"id", "op"});
        if (op == "play_scene" && !play_)
            throw std::runtime_error("play mode is not running");
        return op == "scene" ? scene_.json() : play_->scene.json();
    }
    if (op == "view") {
        require_keys(request, {"id", "op"});
        return view_.json();
    }
    if (op == "status") {
        require_keys(request, {"id", "op"});
        return {{"playing", playing()},
                {"dirty", dirty_},
                {"path", document_path_.string()},
                {"can_undo", !undo_.empty()},
                {"can_redo", !redo_.empty()},
                {"selection", selection_ ? Json(*selection_) : Json(nullptr)}};
    }
    if (op == "stop") {
        require_keys(request, {"id", "op"});
        play_.reset();
        return {{"playing", false}};
    }
    if (op == "tick") {
        require_keys(request, {"id", "op", "ticks", "actions"});
        if (!play_)
            throw std::runtime_error("play mode is not running");
        const auto ticks = tick_count(request);
        const auto input = input_from_json(request.value("actions", Json::object()));
        try {
            for (EntityId i = 0; i < ticks; ++i)
                play_->simulation.tick(1.0 / 60.0, input);
        } catch (...) {
            // Lua/physics state cannot be rolled back. End the failed preview;
            // the authored scene and its undo/redo history have never changed.
            play_.reset();
            throw;
        }
        return {{"ticks", ticks}, {"entities", play_->scene.size()}};
    }
    if (play_)
        throw std::runtime_error("stop play mode before editing or saving the scene");
    if (op == "play") {
        require_keys(request, {"id", "op"});
        auto candidate = std::make_unique<PlayState>(scene_);
        play_ = std::move(candidate);
        return {{"playing", true}};
    }
    if (op == "set_view") {
        require_keys(request, {"id", "op", "transform", "camera"});
        const auto entity = entity_from_json(
            {{"transform", request.at("transform")}, {"camera", request.at("camera")}});
        if (!entity.camera)
            throw std::invalid_argument("editor view requires a camera");
        (void)entity.camera->projection(1);
        const EditorView candidate{entity.transform, *entity.camera};
        const auto result = candidate.json();
        view_ = candidate;
        return result;
    }
    if (op == "focus") {
        require_keys(request, {"id", "op", "entity_id", "aspect"});
        const auto id = request.contains("entity_id") ? entity_id(request.at("entity_id"))
                                                      : selection_.value_or(0);
        const auto& entity = scene_.get(id);
        const auto aspect = viewport_aspect(request);
        auto candidate = view_;
        const auto radius = glm::length(glm::dvec3(entity.transform.scale)) * 0.5;
        const auto tangent =
            std::tan(glm::radians(static_cast<double>(candidate.camera.vertical_fov)) * 0.5) *
            std::min(1.0, static_cast<double>(aspect));
        const auto distance =
            std::max(3.0, radius / std::sin(std::atan(tangent)) + candidate.camera.near_plane);
        candidate.transform.position = glm::vec3(
            glm::dvec3(entity.transform.position) +
            glm::normalize(glm::dquat(candidate.transform.rotation)) * glm::dvec3(0, 0, distance));
        candidate.camera.far_plane = static_cast<float>(
            std::max(static_cast<double>(candidate.camera.far_plane), distance + radius * 2 + 1));
        candidate.transform.validate();
        (void)candidate.camera.projection(aspect);
        const auto result = candidate.json();
        view_ = candidate;
        return result;
    }
    if (op == "pick") {
        require_keys(request, {"id", "op", "screen", "aspect"});
        const auto& screen = request.at("screen");
        if (!screen.is_array() || screen.size() != 2 || !screen[0].is_number() ||
            !screen[1].is_number())
            throw std::invalid_argument("screen requires two numeric coordinates");
        const auto selected =
            pick_entity(scene_, view_.transform, view_.camera, viewport_aspect(request),
                        {screen[0].get<float>(), screen[1].get<float>()});
        const Json result = {{"selection", selected ? Json(*selected) : Json(nullptr)}};
        selection_ = selected;
        return result;
    }
    if (op == "select") {
        require_keys(request, {"id", "op", "entity_id"});
        std::optional<EntityId> selection;
        if (!request.at("entity_id").is_null()) {
            selection = entity_id(request.at("entity_id"));
            (void)scene_.get(*selection);
        }
        selection_ = selection;
        return {{"selection", selection_ ? Json(*selection_) : Json(nullptr)}};
    }
    if (op == "save") {
        require_keys(request, {"id", "op", "path", "overwrite"});
        auto document = encode_scene(scene_);
        std::filesystem::path path = request.at("path").get<std::string>();
        save_scene(scene_, path, request.value("overwrite", false));
        document_path_ = std::move(path);
        saved_document_ = std::move(document);
        dirty_ = false;
        return {{"saved", true}};
    }
    if (op == "undo" || op == "redo") {
        require_keys(request, {"id", "op"});
        auto& source = op == "undo" ? undo_ : redo_;
        auto& destination = op == "undo" ? redo_ : undo_;
        if (source.empty())
            throw std::runtime_error("history is empty");
        const auto dirty = encode_scene(source.back()) != saved_document_;
        push_history(destination, scene_);
        scene_ = std::move(source.back());
        source.pop_back();
        ++revision_;
        dirty_ = dirty;
        selection_.reset();
        return {{"changed", true}};
    }
    Scene candidate = scene_;
    Json result = {{"changed", true}};
    std::optional<std::string> loaded_document;
    std::filesystem::path loaded_path;
    std::optional<EditorView> loaded_view;
    if (op == "load") {
        require_keys(request, {"id", "op", "path"});
        loaded_path = request.at("path").get<std::string>();
        candidate = load_scene(loaded_path);
        loaded_document = encode_scene(candidate);
        loaded_view = scene_view(candidate);
    } else if (op == "batch") {
        require_keys(request, {"id", "op", "commands"});
        const auto& commands = request.at("commands");
        if (!commands.is_array() || commands.empty() || commands.size() > 1024)
            throw std::invalid_argument("batch requires 1..1024 scene commands");
        result = Json::array();
        for (const auto& command : commands)
            result.push_back(mutate(candidate, command));
    } else {
        result = mutate(candidate, request);
    }
    commit(std::move(candidate));
    if (loaded_document) {
        saved_document_ = std::move(*loaded_document);
        document_path_ = std::move(loaded_path);
        view_ = *loaded_view;
        dirty_ = false;
        selection_.reset();
    }
    return result;
}
} // namespace vsim
