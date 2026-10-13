#include <vsim/io.hpp>
#include <vsim/simulation.hpp>

#include <charconv>
#include <iostream>
#include <string_view>

namespace {
int interactive(vsim::Scene& scene, vsim::Simulation& scripts) {
    std::string line;
    for (;;) {
        vsim::Json id = nullptr;
        vsim::Json response;
        bool fatal = false;
        try {
            if (!vsim::read_bounded_line(std::cin, line))
                break;
            const auto request = vsim::parse_json(line);
            if (!request.is_object())
                throw std::invalid_argument("request must be an object");
            id = request.value("id", vsim::Json(nullptr));
            if (!id.is_null() && !id.is_string() && !id.is_number_integer()) {
                throw std::invalid_argument("request id must be null, a string, or an integer");
            }
            const auto op = request.at("op").get<std::string>();
            if (op == "tick") {
                vsim::require_keys(request, {"id", "op", "actions"});
                const auto frame =
                    vsim::input_from_json(request.value("actions", vsim::Json::object()));
                // Callback failures may have partially advanced Lua/scene state. End the session.
                fatal = true;
                scripts.tick(1.0 / 60.0, frame);
            } else if (op == "scene") {
                vsim::require_keys(request, {"id", "op"});
            } else {
                throw std::invalid_argument("runtime supports only scene and tick requests");
            }
            response = {{"id", id}, {"ok", true}, {"result", scene.json()}};
            fatal = false;
        } catch (const std::exception& error) {
            response = {{"id", id}, {"ok", false}, {"error", error.what()}, {"fatal", fatal}};
        }
        std::cout << response.dump(-1, ' ', false, vsim::Json::error_handler_t::replace) << '\n'
                  << std::flush;
        if (fatal || !std::cout || std::cin.bad())
            return 1;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::string scene_path;
        std::string output_path;
        unsigned int ticks = 120;
        bool saw_ticks = false;
        bool interactive_mode = false;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help") {
                std::cout
                    << "Usage: vsim_runtime --scene PATH [--ticks 1..100000] [--output NEW_PATH]\n"
                    << "       vsim_runtime --scene PATH --interactive < input.jsonl\n";
                return 0;
            }
            if (argument == "--interactive" && !interactive_mode) {
                interactive_mode = true;
                continue;
            }
            if (index + 1 >= argc)
                throw std::invalid_argument("option requires a value");
            const std::string_view value(argv[++index]);
            if (argument == "--scene" && scene_path.empty()) {
                if (value.empty())
                    throw std::invalid_argument("--scene requires a nonempty path");
                scene_path = value;
            } else if (argument == "--output" && output_path.empty()) {
                if (value.empty())
                    throw std::invalid_argument("--output requires a nonempty path");
                output_path = value;
            } else if (argument == "--ticks" && !saw_ticks) {
                saw_ticks = true;
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), ticks);
                if (error != std::errc{} || end != value.data() + value.size() || ticks == 0 ||
                    ticks > 100000) {
                    throw std::invalid_argument("ticks must be an integer in 1..100000");
                }
            } else {
                throw std::invalid_argument("unknown or duplicate option: " +
                                            std::string(argument));
            }
        }
        if (scene_path.empty())
            throw std::invalid_argument("--scene is required");
        if (interactive_mode && (saw_ticks || !output_path.empty())) {
            throw std::invalid_argument(
                "--interactive cannot be combined with --ticks or --output");
        }
        auto scene = vsim::load_scene(scene_path);
        vsim::Simulation scripts(scene);
        if (interactive_mode)
            return interactive(scene, scripts);
        for (unsigned int tick = 0; tick < ticks; ++tick)
            scripts.tick(1.0 / 60.0);
        if (!output_path.empty())
            vsim::save_scene(scene, output_path);
        std::cout << vsim::Json({{"ok", true}, {"ticks", ticks}, {"entities", scene.size()}}).dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vsim_runtime: " << error.what() << '\n';
        return 1;
    }
}
