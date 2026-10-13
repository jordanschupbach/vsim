#pragma once

#include <vsim/scene.hpp>
#include <vsim/script.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vsim {
struct RendererConfig {
    std::uint32_t width{1280};
    std::uint32_t height{800};
    bool offscreen{false};
    bool validation{false};
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
    bool draw(const Scene& scene);
    void resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] Image capture();
    [[nodiscard]] std::string device_name() const;
    [[nodiscard]] unsigned int validation_errors() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vsim
