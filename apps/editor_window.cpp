#include <vsim/editor_ui.hpp>
#include <vsim/io.hpp>

#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
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
vsim::Json commands_from_file(const std::string& path, unsigned int frames) {
    if (path.empty())
        return vsim::Json::array();
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open editor commands: " + path);
    std::string contents, line;
    while (vsim::read_bounded_line(stream, line)) {
        if (contents.size() + line.size() + 1 > vsim::max_document_bytes)
            throw std::length_error("editor commands exceed 16 MiB");
        contents += line + '\n';
    }
    auto commands = vsim::parse_json(contents);
    if (!commands.is_array() || commands.size() > 10000)
        throw std::invalid_argument("editor commands must be an array of at most 10000 entries");
    unsigned int previous = 0;
    for (const auto& command : commands) {
        vsim::require_keys(command, {"frame", "request"});
        const auto& number = command.at("frame");
        if (!number.is_number_integer() || number < 0 || number >= frames)
            throw std::invalid_argument("command frame must be within --frames");
        const auto frame = number.get<unsigned int>();
        if (frame < previous || !command.at("request").is_object())
            throw std::invalid_argument("commands must be in frame order with object requests");
        previous = frame;
    }
    return commands;
}
} // namespace
int main(int argc, char** argv) {
    try {
        vsim::RendererConfig config;
        std::string path, capture, commands_path;
        unsigned int frames = 0;
        std::set<std::string> seen;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: vsim_editor [--scene PATH] [--validate] [--x11 | --offscreen "
                             "--frames N]\n"
                             "                   [--capture IMAGE.ppm] [--commands COMMANDS.json]\n"
                             "For JSON lines on stdin, use vsim_editor_cli.\n";
                return 0;
            }
            if (!seen.insert(option).second)
                throw std::invalid_argument("duplicate option: " + option);
            if (option == "--offscreen") {
                config.offscreen = true;
                continue;
            }
            if (option == "--validate") {
                config.validation = true;
                continue;
            }
            if (option == "--x11") {
                config.x11 = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::invalid_argument("missing option value: " + option);
            const std::string value = argv[++i];
            if (value.empty())
                throw std::invalid_argument("empty option value");
            if (option == "--scene")
                path = value;
            else if (option == "--capture")
                capture = value;
            else if (option == "--commands")
                commands_path = value;
            else if (option == "--frames")
                frames = integer(value);
            else
                throw std::invalid_argument("unknown option: " + option);
        }
        if (config.x11 && config.offscreen)
            throw std::invalid_argument("--x11 cannot be combined with --offscreen");
        if ((config.offscreen || !commands_path.empty()) && frames == 0)
            throw std::invalid_argument("offscreen rendering and automation require --frames");
        const auto commands = commands_from_file(commands_path, frames);
        vsim::Editor editor(path.empty() ? vsim::Scene{} : vsim::load_scene(path), path);
        vsim::Renderer renderer(config);
        vsim::EditorUi ui(editor, renderer);
        auto last = std::chrono::steady_clock::now();
        unsigned int rendered = 0;
        std::size_t next_command = 0;
        while (!ui.should_close() && (frames == 0 || rendered < frames)) {
            while (next_command < commands.size() &&
                   commands[next_command].at("frame") == rendered) {
                const auto result = editor.execute(commands[next_command].at("request"));
                std::cout << vsim::Json({{"command", next_command}, {"result", result}}).dump()
                          << '\n';
                ++next_command;
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<float>(now - last).count();
            last = now;
            if (ui.frame(frames ? 1.0F / 60.0F : std::max(elapsed, 0.0001F)))
                ++rendered;
        }
        if (!capture.empty())
            renderer.capture().write_ppm(capture);
        std::cout << vsim::Json({{"ok", true},
                                 {"frames", rendered},
                                 {"status", editor.execute({{"op", "status"}})},
                                 {"validation_errors", renderer.validation_errors()}})
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vsim_editor: " << error.what() << '\n';
        return 1;
    }
}
