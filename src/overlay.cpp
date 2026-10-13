#include "overlay.hpp"

#include <cctype>
#include <map>

namespace vsim {
std::vector<OverlayVertex> overlay_vertices(std::uint32_t width, std::uint32_t height,
                                            const std::array<std::string, 3>& lines) {
    // Original 5x7 bitmap glyphs: no external font or runtime asset dependency.
    static const std::map<char, std::array<unsigned char, 7>> glyphs = {
        {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
        {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
        {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
        {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
        {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
        {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
        {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
        {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
        {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
        {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
        {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
        {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
        {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
        {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
        {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
        {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
        {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
        {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
        {'/', {1, 2, 2, 4, 8, 8, 16}},       {'-', {0, 0, 0, 31, 0, 0, 0}},
        {'!', {4, 4, 4, 4, 4, 0, 4}},        {':', {0, 4, 4, 0, 4, 4, 0}},
        {'.', {0, 0, 0, 0, 0, 0, 4}},        {'|', {4, 4, 4, 4, 4, 4, 4}},
        {',', {0, 0, 0, 0, 0, 4, 8}},        {'?', {14, 17, 1, 2, 4, 0, 4}}};
    std::vector<OverlayVertex> vertices;
    if (width < 200 || height < 180 || lines[0].empty())
        return vertices;
    auto rectangle = [&](float x, float y, float w, float h, glm::vec4 color) {
        const float left = 2 * x / static_cast<float>(width) - 1,
                    right = 2 * (x + w) / static_cast<float>(width) - 1;
        const float top = 1 - 2 * y / static_cast<float>(height),
                    bottom = 1 - 2 * (y + h) / static_cast<float>(height);
        for (auto p : {glm::vec2(left, top),
                       {right, top},
                       {right, bottom},
                       {left, top},
                       {right, bottom},
                       {left, bottom}})
            vertices.push_back({p, color});
    };
    auto text = [&](const std::string& line, float y, float size, glm::vec4 color) {
        float x = 28;
        for (unsigned char c : line) {
            if (x + 6 * size > static_cast<float>(width) - 24)
                break;
            if (c != ' ') {
                auto found = glyphs.find(static_cast<char>(std::toupper(c)));
                const auto& glyph = found == glyphs.end() ? glyphs.at('?') : found->second;
                for (std::size_t row = 0; row < glyph.size(); ++row)
                    for (unsigned int column = 0; column < 5; ++column)
                        if (glyph[row] & (1U << (4 - column)))
                            rectangle(x + static_cast<float>(column) * size,
                                      y + static_cast<float>(row) * size, size, size, color);
            }
            x += 6 * size;
        }
    };
    rectangle(16, 16, static_cast<float>(width) - 32, 80, {0.008F, 0.014F, 0.025F, 1});
    rectangle(16, 16, 4, 80, {0.03F, 0.8F, 0.5F, 1});
    text(lines[0], 28, 3, {0.9F, 0.94F, 1, 1});
    text(lines[1], 64, 2, {0.11F, 0.9F, 0.6F, 1});
    rectangle(16, static_cast<float>(height) - 46, static_cast<float>(width) - 32, 30,
              {0.008F, 0.014F, 0.025F, 1});
    text(lines[2], static_cast<float>(height) - 38, 2, {0.65F, 0.72F, 0.85F, 1});
    return vertices;
}
} // namespace vsim
