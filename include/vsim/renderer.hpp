#pragma once

#include <vsim/scene.hpp>
#include <vsim/script.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
namespace nvrhi {
class IDevice;
}

namespace vsim {
struct RenderFrame;
struct RenderView {
    Transform transform;
    Camera camera;
    // Normalized top-left origin and extent within the output image.
    glm::vec4 bounds{0, 0, 1, 1};
    void validate() const;
};
struct AmbientOcclusion {
    bool enabled{true};
    float radius{1.0F}; // World-space meters.
    float bias{0.035F};
    float strength{1.5F};
    bool visualize{false};
    void validate() const;
};
struct RendererConfig {
    std::uint32_t width{1280};
    std::uint32_t height{800};
    bool offscreen{false};
    bool validation{false};
    AmbientOcclusion ambient_occlusion{};
    // Require an X11 window for native pane embedding; incompatible with offscreen.
    bool x11{false};
};
struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba;
    void write_ppm(const std::filesystem::path& path) const;
};
// Owns the window, Vulkan context, and NVRHI renderer. Call from the main thread.
// This static NVRHI build permits one live Renderer, including offscreen renderers.
class Renderer {
  public:
    explicit Renderer(const RendererConfig& config = {});
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    [[nodiscard]] bool should_close() const;
    [[nodiscard]] InputFrame poll_input();
    void set_title(const std::string& title);
    void set_overlay(std::string title, std::string status, std::string controls);
    void set_ambient_occlusion(const AmbientOcclusion& settings);
    bool draw(const Scene& scene, const RenderView* view = nullptr,
              const std::function<void(const RenderFrame&)>& overlay = {});
    // Borrowed main-thread handles. Extensions must release resources before Renderer.
    [[nodiscard]] GLFWwindow* window() const noexcept;
    [[nodiscard]] nvrhi::IDevice* graphics_device() const noexcept;
    void wait_idle();
    void resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] Image capture();
    // Same, reusing `image`'s storage (for capturing every frame of a video).
    void capture(Image& image);
    [[nodiscard]] std::string device_name() const;
    [[nodiscard]] unsigned int validation_errors() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
