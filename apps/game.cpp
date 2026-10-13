#include <vsim/clock.hpp>
#include <vsim/io.hpp>
#include <vsim/renderer.hpp>
#include <vsim/simulation.hpp>

#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace {
unsigned int integer(const std::string& text) {
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 100000)
        throw std::invalid_argument("expected an integer in 1..100000");
    return value;
}
struct GameState {
    vsim::Scene scene;
    vsim::Simulation simulation;
    explicit GameState(const vsim::Scene& original) : scene(original), simulation(scene) {}
};
std::string status(const vsim::Scene& scene) {
    for (auto id : scene.entities()) {
        const auto& name = scene.get(id).name;
        if (name.starts_with("Status: "))
            return name.substr(8);
    }
    return "Crystal Courier 3D";
}
} // namespace

int main(int argc, char** argv) {
    try {
        vsim::RendererConfig config;
        std::string scene_path, capture_path, output_path;
        unsigned int frames = 0;
        std::set<std::string> seen;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help") {
                std::cout
                    << "Usage: vsim_game --scene PATH [--validate] [--frames N] [--offscreen]\n"
                       "                 [--width N] [--height N] [--capture PATH.ppm] [--output "
                       "NEW_SCENE]\n"
                       "WASD/arrows: move | Space: jump | R: restart | Escape: quit\n";
                return 0;
            }
            if (!seen.insert(argument).second)
                throw std::invalid_argument("duplicate option: " + argument);
            if (argument == "--validate") {
                config.validation = true;
                continue;
            }
            if (argument == "--offscreen") {
                config.offscreen = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::invalid_argument("missing option value");
            const std::string value = argv[++i];
            if (value.empty())
                throw std::invalid_argument("empty option value");
            if (argument == "--scene")
                scene_path = value;
            else if (argument == "--capture")
                capture_path = value;
            else if (argument == "--output")
                output_path = value;
            else if (argument == "--frames")
                frames = integer(value);
            else if (argument == "--width")
                config.width = integer(value);
            else if (argument == "--height")
                config.height = integer(value);
            else
                throw std::invalid_argument("unknown option: " + argument);
        }
        if (scene_path.empty())
            throw std::invalid_argument("--scene is required");
        if (config.offscreen && frames == 0)
            throw std::invalid_argument("--offscreen requires --frames");
        const auto original = vsim::load_scene(scene_path);
        auto game = std::make_unique<GameState>(original);
        vsim::Renderer renderer(config);
        std::cerr << "VSim | NVRHI/Vulkan: " << renderer.device_name() << " | Jolt physics\n"
                  << "WASD/arrows: move | Space: jump | R: restart | Escape: quit\n";
        vsim::FixedClock clock;
        auto last_time = std::chrono::steady_clock::now();
        unsigned int rendered = 0;
        bool restart_held = false;
        while (!renderer.should_close() && (frames == 0 || rendered < frames)) {
            const auto input = renderer.poll_input();
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<double>(now - last_time).count();
            last_time = now;
            const bool restart = input.contains("restart") && input.at("restart") > 0;
            if (restart && !restart_held) {
                game = std::make_unique<GameState>(original);
                clock = vsim::FixedClock();
            }
            restart_held = restart;
            const auto steps = frames ? 1 : clock.advance(elapsed).steps;
            for (std::size_t step = 0; step < steps; ++step)
                game->simulation.tick(clock.step(), input);
            renderer.set_title("Crystal Courier 3D | " + status(game->scene) +
                               " | WASD move / Space jump / R restart / Esc quit");
            renderer.set_overlay("CRYSTAL COURIER / 3D", status(game->scene),
                                 "WASD MOVE | SPACE JUMP | R RESTART | ESC QUIT");
            if (renderer.draw(game->scene))
                ++rendered;
        }
        if (!capture_path.empty())
            renderer.capture().write_ppm(capture_path);
        if (!output_path.empty())
            vsim::save_scene(game->scene, output_path);
        std::cout << vsim::Json({{"ok", true},
                                 {"frames", rendered},
                                 {"renderer", "NVRHI/Vulkan"},
                                 {"physics", "Jolt"},
                                 {"validation_errors", renderer.validation_errors()},
                                 {"status", status(game->scene)}})
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vsim_game: " << error.what() << '\n';
        return 1;
    }
}
