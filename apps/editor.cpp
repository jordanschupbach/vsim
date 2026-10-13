#include <vsim/editor.hpp>
#include <vsim/io.hpp>

#include <iostream>
#include <string_view>

namespace {
int run(vsim::Editor& editor) {
    std::string line;
    for (;;) {
        vsim::Json id = nullptr;
        vsim::Json response;
        try {
            if (!vsim::read_bounded_line(std::cin, line))
                break;
            const auto request = vsim::parse_json(line);
            if (request.is_object() && request.contains("id")) {
                id = request.at("id");
                if (!id.is_null() && !id.is_string() && !id.is_number_integer()) {
                    throw std::invalid_argument("request id must be null, a string, or an integer");
                }
            }
            response = {{"id", id}, {"ok", true}, {"result", editor.execute(request)}};
        } catch (const std::exception& error) {
            response = {{"id", id}, {"ok", false}, {"error", error.what()}};
        }
        std::cout << response.dump(-1, ' ', false, vsim::Json::error_handler_t::replace) << '\n'
                  << std::flush;
        if (!std::cout || std::cin.bad())
            return 1;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Usage: vsim_editor [--scene PATH] < requests.jsonl\n";
            return 0;
        }
        if (argc != 1 && !(argc == 3 && std::string_view(argv[1]) == "--scene")) {
            std::cerr << "Usage: vsim_editor [--scene PATH] < requests.jsonl\n";
            return 2;
        }
        vsim::Editor editor(argc == 3 ? vsim::load_scene(argv[2]) : vsim::Scene{});
        return run(editor);
    } catch (const std::exception& error) {
        std::cerr << "vsim_editor: " << error.what() << '\n';
        return 1;
    }
}
