#include <vsim/editor_ui.hpp>
#include <vsim/io.hpp>

#include "gui_renderer.hpp"

#include <imgui.h>
// ImGuizmo's declarations depend on ImGui types.
#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_stdlib.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace vsim {
namespace {
constexpr auto panel_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
void panel(const char* title, ImVec2 position, ImVec2 size, ImGuiWindowFlags flags = 0) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(title, nullptr, panel_flags | flags);
}
bool vector_field(const char* label, glm::vec3& value, float speed = 0.1F) {
    return ImGui::DragFloat3(label, glm::value_ptr(value), speed);
}
} // namespace
struct EditorUi::Impl {
    Editor& editor;
    Renderer& renderer;
    ImGuiContext* context{};
    bool glfw_initialized{false};
    std::unique_ptr<GuiRenderer> gpu;
    RenderView view;
    std::optional<EntityId> draft_id;
    Entity draft;
    std::uint64_t draft_revision{0};
    bool draft_changed{false};
    bool using_gizmo{false};
    ImGuizmo::OPERATION operation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE mode{ImGuizmo::WORLD};
    std::string path_input;
    std::string error;
    std::string message{"Ready"};
    std::string command_input{"{\"op\":\"describe\"}"};
    std::string command_output;
    std::string prefab_name{"Prefab"};
    bool replace_file{false};
    bool close_requested{false};
    bool closed{false};
    bool open_requested{false};
    bool show_commands{false};
    float accumulator{0};

    Impl(Editor& model, Renderer& graphics)
        : editor(model), renderer(graphics), path_input(model.document_path().string()) {}
    ~Impl() {
        if (context) {
            ImGui::SetCurrentContext(context);
            try {
                renderer.wait_idle();
            } catch (const std::exception& exception) {
                std::cerr << exception.what() << '\n';
            }
            gpu.reset();
            if (glfw_initialized)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(context);
        }
    }
    void initialize() {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = "vsim_nvrhi";
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 0;
        style.FrameRounding = 3;
        style.WindowBorderSize = 1;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.065F, 0.078F, 0.10F, 1);
        style.Colors[ImGuiCol_Header] = ImVec4(0.12F, 0.28F, 0.39F, 1);
        style.Colors[ImGuiCol_Button] = ImVec4(0.13F, 0.25F, 0.34F, 1);
        if (renderer.window()) {
            if (!ImGui_ImplGlfw_InitForVulkan(renderer.window(), true))
                throw std::runtime_error("cannot initialize editor input");
            glfw_initialized = true;
            glfwSetWindowSizeLimits(renderer.window(), 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
        }
        gpu = std::make_unique<GuiRenderer>(*renderer.graphics_device(), *io.Fonts);
        sync_view();
        renderer.set_title("VSim Editor");
    }
    template <typename Fn> bool attempt(Fn function) {
        try {
            function();
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
    void sync_view() {
        view.transform = editor.view().transform;
        view.camera = editor.view().camera;
    }
    void publish_view() {
        const auto data = EditorView{view.transform, view.camera}.json();
        (void)editor.execute({{"op", "set_view"},
                              {"transform", data.at("transform")},
                              {"camera", data.at("camera")}});
    }
    void sync_draft() {
        const auto selection = editor.selection();
        if (selection != draft_id || editor.revision() != draft_revision) {
            draft_id = selection;
            draft_revision = editor.revision();
            draft_changed = false;
            using_gizmo = false;
            if (selection) {
                draft = editor.scene().get(*selection);
            }
        }
    }
    void apply_draft() {
        if (draft_changed && draft_id) {
            (void)editor.execute(
                {{"op", "set"}, {"entity_id", *draft_id}, {"components", entity_json(draft)}});
            draft_changed = false;
            draft_revision = editor.revision();
        }
    }
    void select(std::optional<EntityId> id) {
        apply_draft();
        (void)editor.execute({{"op", "select"}, {"entity_id", id ? Json(*id) : Json(nullptr)}});
        sync_draft();
    }
    void focus() {
        if (!draft_id)
            return;
        attempt([&] {
            apply_draft();
            const auto& io = ImGui::GetIO();
            const auto aspect = (io.DisplaySize.x - 560) / (io.DisplaySize.y - 122);
            (void)editor.execute({{"op", "focus"}, {"entity_id", *draft_id}, {"aspect", aspect}});
            sync_view();
        });
    }
    void save() {
        apply_draft();
        if (editor.document_path().empty())
            throw std::runtime_error("enter a scene path and use Save As");
        (void)editor.execute(
            {{"op", "save"}, {"path", editor.document_path().string()}, {"overwrite", true}});
        message = "Saved " + editor.document_path().string();
    }
    void load() {
        (void)editor.execute({{"op", "load"}, {"path", path_input}});
        sync_draft();
        sync_view();
        message = "Opened " + editor.document_path().string();
    }
    void toolbar(float width) {
        panel("Toolbar", {0, 0}, {width, 92}, ImGuiWindowFlags_NoTitleBar);
        ImGui::TextUnformatted("VSIM / SCENE EDITOR");
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s",
                            editor.document_path().empty()
                                ? "Untitled"
                                : editor.document_path().filename().string().c_str(),
                            editor.dirty() || draft_changed ? " *" : "");
        ImGui::SameLine();
        if (ImGui::Button(editor.playing() ? "Stop" : "Play")) {
            attempt([&] {
                apply_draft();
                (void)editor.execute({{"op", editor.playing() ? "stop" : "play"}});
                accumulator = 0;
            });
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(editor.playing());
        if (ImGui::Button("Save"))
            attempt([&] { save(); });
        ImGui::SameLine();
        const auto status = editor.execute({{"op", "status"}});
        ImGui::BeginDisabled(draft_changed || !status.at("can_undo").get<bool>());
        if (ImGui::Button("Undo"))
            attempt([&] {
                (void)editor.execute({{"op", "undo"}});
                sync_draft();
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(draft_changed || !status.at("can_redo").get<bool>());
        if (ImGui::Button("Redo"))
            attempt([&] {
                (void)editor.execute({{"op", "redo"}});
                sync_draft();
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::RadioButton("Move", operation == ImGuizmo::TRANSLATE))
            operation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", operation == ImGuizmo::ROTATE))
            operation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", operation == ImGuizmo::SCALE))
            operation = ImGuizmo::SCALE;
        ImGui::SameLine();
        bool local = mode == ImGuizmo::LOCAL;
        if (ImGui::Checkbox("Local", &local))
            mode = local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        ImGui::SetNextItemWidth(std::max(180.0F, width - 420));
        ImGui::InputText("##scene_path", &path_input);
        ImGui::SameLine();
        if (ImGui::Button("Open")) {
            if (editor.dirty() || draft_changed)
                open_requested = true;
            else
                attempt([&] { load(); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As"))
            attempt([&] {
                apply_draft();
                (void)editor.execute(
                    {{"op", "save"}, {"path", path_input}, {"overwrite", replace_file}});
                message = "Saved " + path_input;
            });
        ImGui::SameLine();
        ImGui::Checkbox("Replace", &replace_file);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Commands", &show_commands);
        ImGui::End();
    }
    void hierarchy(float height) {
        panel("Scene", {0, 92}, {230, height - 122});
        ImGui::BeginDisabled(editor.playing());
        if (ImGui::Button("Add cube"))
            attempt([&] {
                apply_draft();
                const auto result =
                    editor.execute({{"op", "create"},
                                    {"components", {{"name", "Cube"}, {"mesh", Json::object()}}}});
                select(result.at("entity_id").get<EntityId>());
            });
        ImGui::SameLine();
        if (ImGui::Button("Add entity"))
            attempt([&] {
                apply_draft();
                select(editor.execute({{"op", "create"}, {"components", Json::object()}})
                           .at("entity_id")
                           .get<EntityId>());
            });
        ImGui::Separator();
        ImGui::BeginChild("Entities", {0, -180});
        for (const auto id : editor.scene().entities()) {
            const auto& entity = editor.scene().get(id);
            const auto label = entity.name + "##" + std::to_string(id);
            if (ImGui::Selectable(label.c_str(), editor.selection() == id))
                attempt([&] { select(id); });
        }
        ImGui::EndChild();
        ImGui::SeparatorText("Prefabs");
        ImGui::InputText("##prefab", &prefab_name);
        ImGui::BeginDisabled(!draft_id);
        if (ImGui::Button("Save selection as prefab"))
            attempt([&] {
                apply_draft();
                (void)editor.execute(
                    {{"op", "prefab"}, {"name", prefab_name}, {"components", entity_json(draft)}});
            });
        ImGui::EndDisabled();
        if (ImGui::Button("Spawn prefab"))
            attempt([&] {
                apply_draft();
                select(editor.execute({{"op", "spawn"}, {"name", prefab_name}})
                           .at("entity_id")
                           .get<EntityId>());
            });
        ImGui::EndDisabled();
        ImGui::End();
    }
    void inspector(float width, float height) {
        panel("Inspector", {width - 330, 92}, {330, height - 122});
        ImGui::BeginDisabled(editor.playing());
        if (!draft_id) {
            ImGui::TextWrapped("Select an entity in the scene or click a mesh in the viewport.");
        } else {
            ImGui::Text("Entity %llu", static_cast<unsigned long long>(*draft_id));
            draft_changed |= ImGui::InputText("Name", &draft.name);
            if (ImGui::Button("Apply"))
                attempt([&] { apply_draft(); });
            ImGui::SameLine();
            if (ImGui::Button("Revert")) {
                draft = editor.scene().get(*draft_id);
                draft_changed = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Focus"))
                focus();
            if (draft_changed)
                ImGui::TextDisabled("Unapplied component changes");
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                draft_changed |= vector_field("Position", draft.transform.position);
                auto angles = glm::degrees(glm::eulerAngles(draft.transform.rotation));
                if (vector_field("Rotation", angles, 0.5F)) {
                    draft.transform.rotation = glm::normalize(glm::quat(glm::radians(angles)));
                    draft_changed = true;
                }
                draft_changed |= vector_field("Scale", draft.transform.scale, 0.01F);
            }
            auto component = [&](const char* label, auto& value) {
                bool present = value.has_value();
                if (ImGui::Checkbox(label, &present)) {
                    if (present)
                        value.emplace();
                    else
                        value.reset();
                    draft_changed = true;
                }
                return present;
            };
            ImGui::Separator();
            if (component("Mesh", draft.mesh)) {
                auto& mesh = *draft.mesh;
                const char* primitives[]{"box", "sphere", "crystal"};
                int index = mesh.primitive == "sphere" ? 1 : mesh.primitive == "crystal" ? 2 : 0;
                if (ImGui::Combo("Primitive", &index, primitives, 3)) {
                    mesh.primitive = primitives[index];
                    draft_changed = true;
                }
                draft_changed |= ImGui::ColorEdit3("Albedo (linear)", glm::value_ptr(mesh.color));
                draft_changed |= ImGui::SliderFloat("Metallic", &mesh.metallic, 0, 1);
                draft_changed |= ImGui::SliderFloat("Roughness", &mesh.roughness, 0.04F, 1);
                draft_changed |= ImGui::DragFloat("Emissive", &mesh.emissive, 0.1F, 0, 10);
            }
            ImGui::Separator();
            if (component("Rigid body", draft.rigid_body)) {
                auto& body = *draft.rigid_body;
                const char* motions[]{"static", "dynamic", "kinematic"};
                int motion = body.motion == "dynamic" ? 1 : body.motion == "kinematic" ? 2 : 0;
                if (ImGui::Combo("Motion", &motion, motions, 3)) {
                    body.motion = motions[motion];
                    draft_changed = true;
                }
                const char* shapes[]{"box", "sphere"};
                int shape = body.shape == "sphere" ? 1 : 0;
                if (ImGui::Combo("Shape", &shape, shapes, 2)) {
                    body.shape = shapes[shape];
                    draft_changed = true;
                }
                draft_changed |= ImGui::DragFloat("Mass", &body.mass, 0.1F);
                draft_changed |= ImGui::DragFloat("Friction", &body.friction, 0.01F);
                draft_changed |= ImGui::SliderFloat("Restitution", &body.restitution, 0, 1);
                draft_changed |= ImGui::DragFloat("Gravity scale", &body.gravity_scale, 0.01F);
                draft_changed |= ImGui::Checkbox("Sensor", &body.sensor);
                draft_changed |= ImGui::Checkbox("Lock rotation", &body.lock_rotation);
                draft_changed |= vector_field("Velocity", body.linear_velocity);
            }
            ImGui::Separator();
            if (component("Camera", draft.camera)) {
                draft_changed |=
                    ImGui::DragFloat("Field of view", &draft.camera->vertical_fov, 0.5F);
                draft_changed |= ImGui::DragFloat("Near plane", &draft.camera->near_plane, 0.01F);
                draft_changed |= ImGui::DragFloat("Far plane", &draft.camera->far_plane, 1);
            }
            ImGui::Separator();
            if (component("Lua script", draft.script)) {
                draft_changed |= ImGui::InputTextMultiline(
                    "##source", &draft.script->source, {0, 220}, ImGuiInputTextFlags_AllowTabInput);
            }
            ImGui::Separator();
            if (ImGui::Button("Duplicate"))
                attempt([&] {
                    apply_draft();
                    auto copy = draft;
                    copy.name += " copy";
                    copy.transform.position.x += 1;
                    select(editor.execute({{"op", "create"}, {"components", entity_json(copy)}})
                               .at("entity_id")
                               .get<EntityId>());
                });
            ImGui::SameLine();
            if (ImGui::Button("Delete"))
                attempt([&] {
                    (void)editor.execute({{"op", "destroy"}, {"entity_id", *draft_id}});
                    sync_draft();
                });
        }
        ImGui::EndDisabled();
        ImGui::End();
    }
    void viewport(float width, float height, float delta) {
        const ImVec2 origin{230, 92}, extent{width - 560, height - 122};
        view.bounds = {origin.x / width, origin.y / height, extent.x / width, extent.y / height};
        panel("Viewport", origin, extent,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
        auto& io = ImGui::GetIO();
        if (!editor.playing()) {
            const bool hovered = ImGui::IsWindowHovered();
            bool navigated = false;
            if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0)) {
                const auto yaw = glm::angleAxis(-io.MouseDelta.x * 0.004F, glm::vec3(0, 1, 0));
                const auto pitch = glm::angleAxis(-io.MouseDelta.y * 0.004F, glm::vec3(1, 0, 0));
                view.transform.rotation = glm::normalize(yaw * view.transform.rotation * pitch);
                navigated = true;
            }
            if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                auto key = [](ImGuiKey code) { return ImGui::IsKeyDown(code) ? 1.0F : 0.0F; };
                glm::vec3 movement(key(ImGuiKey_D) - key(ImGuiKey_A),
                                   key(ImGuiKey_E) - key(ImGuiKey_Q),
                                   key(ImGuiKey_S) - key(ImGuiKey_W));
                if (glm::length(movement) > 0) {
                    view.transform.position += view.transform.rotation * glm::normalize(movement) *
                                               delta * (io.KeyShift ? 20.0F : 7.0F);
                    navigated = true;
                }
            }
            if (hovered && io.MouseWheel != 0) {
                view.transform.position +=
                    view.transform.rotation * glm::vec3(0, 0, -io.MouseWheel * 1.5F);
                navigated = true;
            }
            if (navigated && !attempt([&] { publish_view(); }))
                sync_view();
            if (hovered && ImGui::IsKeyPressed(ImGuiKey_F))
                focus();
            bool valid_transform = true;
            try {
                draft.transform.validate();
            } catch (const std::exception&) {
                valid_transform = false;
            }
            if (draft_id && valid_transform) {
                auto matrix = draft.transform.matrix();
                const auto camera = glm::mat4_cast(glm::conjugate(view.transform.rotation)) *
                                    glm::translate(glm::mat4(1), -view.transform.position);
                // ImGuizmo expects the conventional OpenGL clip-depth projection.
                auto projection = glm::perspectiveRH_NO(glm::radians(view.camera.vertical_fov),
                                                        extent.x / extent.y, view.camera.near_plane,
                                                        view.camera.far_plane);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(origin.x, origin.y, extent.x, extent.y);
                ImGuizmo::SetOrthographic(false);
                if (ImGuizmo::Manipulate(glm::value_ptr(camera), glm::value_ptr(projection),
                                         operation, mode, glm::value_ptr(matrix))) {
                    glm::vec3 scale, translation, skew;
                    glm::vec4 perspective;
                    glm::quat rotation;
                    if (glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
                        draft.transform = {translation, glm::normalize(rotation), scale};
                        draft_changed = true;
                    }
                }
                if (using_gizmo && !ImGuizmo::IsUsing())
                    attempt([&] { apply_draft(); });
                using_gizmo = ImGuizmo::IsUsing();
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing()) {
                const glm::vec2 screen{(io.MousePos.x - origin.x) / extent.x,
                                       (io.MousePos.y - origin.y) / extent.y};
                attempt([&] {
                    apply_draft();
                    (void)editor.execute({{"op", "pick"},
                                          {"screen", {screen.x, screen.y}},
                                          {"aspect", extent.x / extent.y}});
                    sync_draft();
                });
            }
        } else {
            accumulator += std::min(delta, 0.25F);
            while (accumulator >= 1.0F / 60.0F && editor.playing()) {
                Json input = Json::object();
                if (ImGui::IsWindowHovered() && !io.WantTextInput) {
                    auto key = [](ImGuiKey code) { return ImGui::IsKeyDown(code) ? 1.0F : 0.0F; };
                    input = {{"move_x", key(ImGuiKey_D) - key(ImGuiKey_A)},
                             {"move_z", key(ImGuiKey_S) - key(ImGuiKey_W)},
                             {"jump", key(ImGuiKey_Space)},
                             {"restart", key(ImGuiKey_R)}};
                }
                attempt([&] {
                    (void)editor.execute({{"op", "tick"}, {"ticks", 1}, {"actions", input}});
                });
                accumulator -= 1.0F / 60.0F;
            }
        }
        ImGui::End();
    }
    void confirmations() {
        if (open_requested || close_requested)
            ImGui::OpenPopup("Unsaved scene");
        if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your scene before continuing?");
            if (ImGui::Button("Save and continue")) {
                if (attempt([&] {
                        save();
                        if (open_requested)
                            load();
                    })) {
                    closed = close_requested;
                    open_requested = close_requested = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard changes")) {
                if (!open_requested || attempt([&] { load(); })) {
                    closed = close_requested;
                    open_requested = close_requested = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open_requested = close_requested = false;
                ImGui::CloseCurrentPopup();
            }
            if (!error.empty())
                ImGui::TextWrapped("%s", error.c_str());
            ImGui::EndPopup();
        }
    }
    bool frame(float delta, std::uint32_t offscreen_width, std::uint32_t offscreen_height) {
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        if (renderer.window()) {
            glfwPollEvents();
            if (renderer.should_close()) {
                glfwSetWindowShouldClose(renderer.window(), GLFW_FALSE);
                if (editor.playing())
                    (void)editor.execute({{"op", "stop"}});
                if (editor.dirty() || draft_changed)
                    close_requested = true;
                else
                    closed = true;
            }
            ImGui_ImplGlfw_NewFrame();
        } else {
            io.DisplaySize = {static_cast<float>(offscreen_width),
                              static_cast<float>(offscreen_height)};
            io.DisplayFramebufferScale = {1, 1};
        }
        if (io.DisplaySize.x < 600 || io.DisplaySize.y < 300) {
            if (renderer.window())
                glfwWaitEventsTimeout(0.05);
            return false;
        }
        io.DeltaTime = std::clamp(delta, 0.0001F, 0.25F);
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        sync_draft();
        sync_view();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !editor.playing())
            attempt([&] { save(); });
        toolbar(io.DisplaySize.x);
        hierarchy(io.DisplaySize.y);
        inspector(io.DisplaySize.x, io.DisplaySize.y);
        viewport(io.DisplaySize.x, io.DisplaySize.y, io.DeltaTime);
        panel("Status", {0, io.DisplaySize.y - 30}, {io.DisplaySize.x, 30},
              ImGuiWindowFlags_NoTitleBar);
        ImGui::TextUnformatted(error.empty()
                                   ? "RMB + WASD/QE: fly | Wheel: dolly | F: focus | Ctrl+S: save"
                                   : error.c_str());
        ImGui::End();
        if (show_commands) {
            ImGui::SetNextWindowSize({700, 350}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Agent commands", &show_commands);
            ImGui::InputTextMultiline("Request", &command_input, {-1, 80});
            if (ImGui::Button("Execute"))
                attempt([&] {
                    apply_draft();
                    command_output = editor.execute(parse_json(command_input)).dump(2);
                    sync_draft();
                    sync_view();
                });
            ImGui::TextWrapped("%s", command_output.c_str());
            ImGui::End();
        }
        confirmations();
        ImGui::Render();
        std::optional<Scene> draft_preview;
        if (!editor.playing() && draft_changed && draft_id) {
            // Invalid inspector input remains editable without interrupting rendering.
            try {
                draft.validate();
                draft_preview = editor.scene();
                draft_preview->set(*draft_id, draft);
            } catch (const std::invalid_argument&) {
                draft_preview.reset();
            }
        }
        const auto& preview = draft_preview ? *draft_preview : editor.displayed_scene();
        RenderView active_view = view;
        if (editor.playing()) {
            for (const auto id : preview.entities()) {
                const auto& entity = preview.get(id);
                if (entity.camera) {
                    active_view.transform = entity.transform;
                    active_view.camera = *entity.camera;
                    break;
                }
            }
        }
        return renderer.draw(preview, &active_view, [&](const RenderFrame& frame) {
            gpu->draw(frame, *ImGui::GetDrawData());
        });
    }
};
EditorUi::EditorUi(Editor& editor, Renderer& renderer)
    : impl_(std::make_unique<Impl>(editor, renderer)) {
    impl_->initialize();
}
EditorUi::~EditorUi() = default;
bool EditorUi::frame(float delta, std::uint32_t width, std::uint32_t height) {
    if (!std::isfinite(delta) || delta <= 0)
        throw std::invalid_argument("editor frame delta must be finite and positive");
    return impl_->frame(delta, width, height);
}
bool EditorUi::should_close() const noexcept {
    return impl_->closed;
}
} // namespace vsim
