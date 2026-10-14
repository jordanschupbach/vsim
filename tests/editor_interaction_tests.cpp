#include <vsim/editor_ui.hpp>

#include <imgui.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error("check failed: " #condition);                                 \
    } while (false)
} // namespace
int main() {
    try {
        using namespace vsim;
        Editor editor;
        Transform camera;
        camera.position = {0, 0, 5};
        const auto view = EditorView{camera, {}}.json();
        (void)editor.execute({{"op", "set_view"},
                              {"transform", view.at("transform")},
                              {"camera", view.at("camera")}});
        RendererConfig config;
        config.offscreen = true;
        config.validation = true;
        Renderer renderer(config);
        {
            EditorUi ui(editor, renderer);
            auto frame = [&] { CHECK(ui.frame(1.0F / 60)); };
            auto move = [&](float x, float y) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                frame();
            };
            auto click = [&](float x, float y) {
                move(x, y);
                ImGui::GetIO().AddMouseButtonEvent(0, true);
                frame();
                ImGui::GetIO().AddMouseButtonEvent(0, false);
                frame();
            };
            frame();
            frame();
            // Exercise the same input events used by the GLFW backend. Coordinates
            // are logical pixels in the default 1280x800 editor layout.
            click(40, 128); // Add cube in the hierarchy.
            CHECK(editor.scene().size() == 1);
            CHECK(editor.selection());
            const auto id = *editor.selection();
            CHECK(editor.scene().get(id).mesh);
            CHECK(editor.dirty());
            (void)editor.execute({{"op", "select"}, {"entity_id", nullptr}});
            frame();
            click(590, 431); // Mesh center in the viewport.
            CHECK(editor.selection() == id);
            const auto before = editor.scene().get(id).transform.position;
            const auto revision = editor.revision();
            move(620, 431); // Translate gizmo's X axis.
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            frame();
            move(650, 431);
            move(680, 431);
            CHECK(editor.revision() == revision); // Drag is an uncommitted preview.
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frame();
            const auto after = editor.scene().get(id).transform.position;
            CHECK(after.x > before.x + 0.2F);
            CHECK(std::abs(after.y - before.y) < 0.001F);
            CHECK(std::abs(after.z - before.z) < 0.001F);
            CHECK(editor.revision() == revision + 1);
            (void)editor.execute({{"op", "undo"}});
            CHECK(editor.scene().get(id).transform.position == before);
            (void)editor.execute({{"op", "redo"}});
            CHECK(editor.scene().get(id).transform.position == after);
            frame();
            // Camera navigation is editor state, so it cannot create scene undo entries.
            const auto saved_revision = editor.revision();
            const auto camera_before = editor.view().transform.position;
            move(590, 500);
            ImGui::GetIO().AddMouseWheelEvent(0, 1);
            frame();
            CHECK(editor.view().transform.position != camera_before);
            CHECK(editor.revision() == saved_revision);
            const auto authored = editor.execute({{"op", "scene"}});
            click(245, 17); // Play button.
            CHECK(editor.playing());
            frame();
            click(245, 17); // Stop button.
            CHECK(!editor.playing());
            CHECK(editor.execute({{"op", "scene"}}) == authored);
        }
        renderer.wait_idle();
        CHECK(renderer.validation_errors() == 0);
        std::cout
            << "PASS GUI creation, picking, gizmo drag/undo, navigation, play/stop, lifetime\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
