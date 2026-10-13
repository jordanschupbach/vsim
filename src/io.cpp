#include <vsim/io.hpp>

#include <array>
#include <fstream>
#include <random>
#include <stdexcept>
#include <unordered_set>

namespace vsim {
Json parse_json(std::string_view document) {
    if (document.size() > max_document_bytes) {
        throw std::length_error("JSON document exceeds 16 MiB");
    }
    std::vector<std::unordered_set<std::string>> keys;
    return Json::parse(document, [&keys](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 64) {
            throw std::invalid_argument("JSON nesting exceeds 64 levels");
        }
        if (event == Json::parse_event_t::object_start) {
            keys.emplace_back();
        } else if (event == Json::parse_event_t::object_end) {
            keys.pop_back();
        } else if (event == Json::parse_event_t::key) {
            if (!keys.back().insert(value.get<std::string>()).second) {
                throw std::invalid_argument("duplicate JSON key");
            }
        }
        return true;
    });
}

bool read_bounded_line(std::istream& input, std::string& line, std::size_t limit) {
    line.clear();
    bool overflow = false;
    bool any = false;
    char byte = 0;
    while (input.get(byte)) {
        any = true;
        if (byte == '\n')
            break;
        if (line.size() < limit)
            line.push_back(byte);
        else
            overflow = true;
    }
    if (input.bad())
        throw std::runtime_error("input read failed");
    if (overflow)
        throw std::length_error("request exceeds size limit");
    return any;
}

Scene load_scene(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open scene: " + path.string());
    std::string document;
    std::array<char, 8192> buffer{};
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
           input.gcount()) {
        if (document.size() + static_cast<std::size_t>(input.gcount()) > max_document_bytes) {
            throw std::length_error("scene exceeds 16 MiB");
        }
        document.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    }
    if (input.bad())
        throw std::runtime_error("scene read failed");
    return Scene::from_json(parse_json(document));
}

std::string encode_scene(const Scene& scene) {
    const auto document = scene.json().dump(2) + '\n';
    if (document.size() > max_document_bytes)
        throw std::length_error("scene exceeds 16 MiB");
    return document;
}

void save_scene(const Scene& scene, const std::filesystem::path& path) {
    const auto document = encode_scene(scene);
    if (std::filesystem::exists(path))
        throw std::runtime_error("output already exists");
    auto parent = path.parent_path();
    if (parent.empty())
        parent = ".";
    // Exclusive directory creation reserves a private temporary name across processes.
    std::random_device random;
    std::filesystem::path temporary;
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate = parent / (".vsim-save-" + std::to_string(random()));
        if (std::filesystem::create_directory(candidate)) {
            temporary = std::move(candidate);
            break;
        }
    }
    if (temporary.empty())
        throw std::runtime_error("cannot reserve temporary scene path");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{temporary};
    const auto staging = temporary / "scene.json";
    std::ofstream output(staging, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(document.data(), static_cast<std::streamsize>(document.size()));
    output.close();
    // A hard link atomically publishes complete bytes and fails if the destination exists.
    // The staging path is on the same filesystem. No non-atomic fallback is attempted.
    std::filesystem::create_hard_link(staging, path);
}
} // namespace vsim
