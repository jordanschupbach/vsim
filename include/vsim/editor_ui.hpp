#pragma once

#include <vsim/editor.hpp>
#include <vsim/renderer.hpp>

#include <filesystem>
#include <memory>

namespace vsim {
// Main-thread graphical frontend. Editor and Renderer must outlive this object.
class EditorUi {
  public:
    EditorUi(Editor& editor, Renderer& renderer);
    ~EditorUi();
    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;
    // Offscreen callers supply logical display dimensions; desktop uses GLFW.
    bool frame(float delta, std::uint32_t width = 1280, std::uint32_t height = 800);
    [[nodiscard]] bool should_close() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
