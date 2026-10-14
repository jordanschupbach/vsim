#pragma once

#include <vsim/scene.hpp>

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

namespace vsim {
inline constexpr std::size_t max_document_bytes = 16 * 1024 * 1024;
[[nodiscard]] Json parse_json(std::string_view document);
[[nodiscard]] std::string encode_scene(const Scene& scene);
[[nodiscard]] Scene load_scene(const std::filesystem::path& path);
// Atomic publication. Existing paths are preserved unless overwrite is explicit.
// Overwrite replaces the directory entry, never writes through a symlink.
void save_scene(const Scene& scene, const std::filesystem::path& path, bool overwrite = false);
// Consumes an overlong line before throwing, permitting the next request to be read.
[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line,
                                     std::size_t limit = max_document_bytes);
} // namespace vsim
