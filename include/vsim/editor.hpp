#pragma once

#include <vsim/scene.hpp>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vsim {
struct EditorView {
    Transform transform;
    Camera camera;
    [[nodiscard]] Json json() const;
};
// Shared authoring model for the graphical editor and JSON command service.
// All access is confined to the application's main thread.
class Editor {
  public:
    explicit Editor(Scene scene = {}, std::filesystem::path document_path = {});
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    [[nodiscard]] Json execute(const Json& request);
    [[nodiscard]] const Scene& scene() const noexcept;
    [[nodiscard]] const Scene& displayed_scene() const noexcept;
    [[nodiscard]] const EditorView& view() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::filesystem::path& document_path() const noexcept;
    [[nodiscard]] std::optional<EntityId> selection() const noexcept;

  private:
    struct PlayState;
    Scene scene_;
    EditorView view_;
    std::deque<Scene> undo_;
    std::deque<Scene> redo_;
    std::unique_ptr<PlayState> play_;
    std::optional<EntityId> selection_;
    std::string saved_document_;
    bool dirty_{false};
    std::uint64_t revision_{0};
    std::filesystem::path document_path_;
    void commit(Scene scene);
};
} // namespace vsim
