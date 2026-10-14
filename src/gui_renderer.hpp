#pragma once

#include <vsim/renderer_extension.hpp>

#include <memory>

struct ImDrawData;
struct ImFontAtlas;
namespace vsim {
// ImGui renderer backend. It borrows the device; release after GPU idle and
// before destroying the Renderer. ImGui's current context owns the atlas.
class GuiRenderer {
  public:
    GuiRenderer(nvrhi::IDevice& device, ImFontAtlas& fonts);
    ~GuiRenderer();
    GuiRenderer(const GuiRenderer&) = delete;
    GuiRenderer& operator=(const GuiRenderer&) = delete;
    void draw(const RenderFrame& frame, const ImDrawData& data);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
