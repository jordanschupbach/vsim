#include <vsim/io.hpp>
#include <vsim/renderer.hpp>
#include <vsim/simulation.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
int passed = 0, failed = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            throw std::runtime_error("check failed: " #condition);                                 \
    } while (false)
void test(const char* name, const std::function<void()>& run) {
    try {
        run();
        ++passed;
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
template <typename Fn> void rejects(Fn run) {
    bool threw = false;
    try {
        run();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}
vsim::Entity camera(glm::vec3 position, glm::vec3 target = {}) {
    vsim::Entity entity;
    entity.camera = vsim::Camera{};
    entity.transform.position = position;
    entity.transform.rotation =
        glm::quat_cast(glm::inverse(glm::lookAtRH(position, target, glm::vec3(0, 1, 0))));
    return entity;
}
vsim::Entity mesh(glm::vec3 position, glm::vec3 color, glm::vec3 scale = glm::vec3(1)) {
    vsim::Entity entity;
    entity.transform.position = position;
    entity.transform.scale = scale;
    entity.mesh = vsim::Mesh{};
    entity.mesh->color = color;
    return entity;
}
std::array<int, 3> pixel(const vsim::Image& image, std::uint32_t x, std::uint32_t y) {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.rgba.at(offset), image.rgba.at(offset + 1), image.rgba.at(offset + 2)};
}
std::array<std::uint32_t, 2> project(const vsim::Entity& view, glm::vec3 point, std::uint32_t width,
                                     std::uint32_t height) {
    const auto clip =
        view.camera->projection(static_cast<float>(width) / static_cast<float>(height)) *
        glm::inverse(view.transform.matrix()) * glm::vec4(point, 1);
    return {
        static_cast<std::uint32_t>((clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(width)),
        static_cast<std::uint32_t>((clip.y / clip.w * 0.5F + 0.5F) * static_cast<float>(height))};
}
} // namespace

int main(int argc, char** argv) {
    using namespace vsim;
    const bool window = argc == 2 && std::string_view(argv[1]) == "--window";
    try {
        test("invalid dimensions fail before GPU creation", [] {
            rejects([] { Renderer invalid({0, 240, true, true}); });
            rejects([] { Renderer invalid({320, 4097, true, true}); });
            rejects([] { Image{0, 0, {}}.write_ppm("invalid.ppm"); });
            rejects([] { Image{0x80000000U, 0x80000000U, {}}.write_ppm("invalid.ppm"); });
        });
        {
            Renderer renderer({320, 240, !window, true});
            std::cout << "Device: " << renderer.device_name() << '\n';
            auto draw = [&](const Scene& scene) {
                for (int attempt = 0; attempt < 20; ++attempt) {
                    (void)renderer.poll_input();
                    if (renderer.draw(scene))
                        return;
                }
                throw std::runtime_error("no frame rendered after 20 attempts");
            };
            test("invalid lifecycle operations preserve renderer usability", [&] {
                rejects([&] { (void)renderer.capture(); });
                rejects([&] { Renderer overlapping({320, 240, true, true}); });
                rejects([&] { renderer.resize(0, 240); });
                rejects([&] { renderer.set_overlay(std::string(129, 'A'), "", ""); });
                rejects([&] { draw(Scene{}); });
            });
            test("near red geometry occludes later blue geometry with correct readback channels",
                 [&] {
                     Scene scene;
                     (void)scene.create(camera({0, 0, 5}));
                     const auto red = scene.create(mesh({0, 0, 1}, {0.9F, 0.01F, 0.01F}));
                     (void)scene.create(mesh({0, 0, 0}, {0.01F, 0.01F, 0.9F}, glm::vec3(2)));
                     draw(scene);
                     auto image = renderer.capture();
                     const auto first = pixel(image, image.width / 2, image.height / 2);
                     CHECK(first[0] > first[2] + 70);
                     scene.destroy(red);
                     draw(scene);
                     image = renderer.capture();
                     const auto second = pixel(image, image.width / 2, image.height / 2);
                     CHECK(second[2] > second[0] + 70);
                 });
            test("directional shadow darkens the geometrically expected ground location", [&] {
                Scene scene;
                const auto view = camera({0, 8, 10});
                (void)scene.create(view);
                (void)scene.create(mesh({0, -0.25F, 0}, glm::vec3(0.5F), {10, 0.5F, 10}));
                const auto caster = scene.create(mesh({0, 2, 0}, glm::vec3(0.5F)));
                draw(scene);
                const auto shadow = renderer.capture();
                const auto at = project(view, {2 * 0.5F / 0.85F, 0, -2 * 0.4F / 0.85F},
                                        shadow.width, shadow.height);
                const auto dark = pixel(shadow, at[0], at[1]);
                scene.destroy(caster);
                draw(scene);
                const auto light = pixel(renderer.capture(), at[0], at[1]);
                CHECK(light[0] > dark[0] + 20);
            });
            test("material emission changes HDR lighting through the tonemap", [&] {
                Scene scene;
                (void)scene.create(camera({0, 0, 5}));
                const auto id = scene.create(mesh({0, 0, 0}, glm::vec3(0.1F)));
                draw(scene);
                auto image = renderer.capture();
                const auto dark = pixel(image, image.width / 2, image.height / 2);
                auto changed = scene.get(id);
                changed.mesh->emissive = 4;
                scene.set(id, changed);
                draw(scene);
                image = renderer.capture();
                const auto lit = pixel(image, image.width / 2, image.height / 2);
                CHECK(lit[0] > dark[0] + 40 && lit[0] < 255);
                changed.mesh->emissive = 8;
                scene.set(id, changed);
                draw(scene);
                image = renderer.capture();
                CHECK(pixel(image, image.width / 2, image.height / 2)[0] > lit[0]);
            });
            test("positive world Y projects above center and HUD stays at the top", [&] {
                Scene scene;
                const auto view = camera({0, 0, 5});
                (void)scene.create(view);
                (void)scene.create(mesh({0, 1, 0}, {0.9F, 0.01F, 0.01F}));
                draw(scene);
                const auto plain = renderer.capture();
                const auto at = project(view, {0, 1, 0}, plain.width, plain.height);
                CHECK(at[1] < plain.height / 2);
                const auto red = pixel(plain, at[0], at[1]);
                CHECK(red[0] > red[2] + 60);
                renderer.set_overlay("TEST", "STATUS", "CONTROLS");
                draw(scene);
                const auto hud = renderer.capture();
                CHECK(pixel(plain, 18, 18) != pixel(hud, 18, 18));
                renderer.set_overlay("", "", "");
            });
            test(
                "odd readback strides resizing and pending-frame resource churn remain valid", [&] {
                    Scene scene;
                    (void)scene.create(camera({0, 0, 5}));
                    for (const auto dimensions :
                         {std::array{257U, 193U}, std::array{640U, 360U}, std::array{320U, 240U}}) {
                        renderer.resize(dimensions[0], dimensions[1]);
                        for (int i = 0; i < 6; ++i) {
                            const auto id = scene.create(mesh({0, 0, 0}, {0.02F, 0.8F, 0.02F}));
                            draw(scene);
                            scene.destroy(id);
                        }
                        const auto image = renderer.capture();
                        if (!window)
                            CHECK(image.width == dimensions[0] && image.height == dimensions[1]);
                        // Window sizes are logical units; high-DPI framebuffers may be larger.
                        CHECK(std::abs(static_cast<float>(image.width) /
                                           static_cast<float>(image.height) -
                                       static_cast<float>(dimensions[0]) /
                                           static_cast<float>(dimensions[1])) < 0.03F);
                        CHECK(image.rgba.size() ==
                              static_cast<std::size_t>(image.width) * image.height * 4);
                        const auto green = pixel(image, image.width / 2, image.height / 2);
                        CHECK(green[1] > green[0] + 60);
                    }
                });
            test("the complete game scene simulates and renders all primitive materials", [&] {
                auto scene = load_scene(std::string(VSIM_SOURCE_DIR) +
                                        "/examples/crystal_courier_3d/scene.json");
                Simulation simulation(scene);
                for (int i = 0; i < 120; ++i)
                    simulation.tick(1.0 / 60.0);
                renderer.resize(640, 480);
                renderer.set_overlay("CRYSTAL COURIER / 3D", "0 / 5 CRYSTALS",
                                     "WASD MOVE | SPACE JUMP");
                draw(scene);
                const auto image = renderer.capture();
                std::size_t teal = 0, pink = 0, gold = 0;
                for (std::uint32_t y = 110; y < image.height - 60; ++y) {
                    for (std::uint32_t x = 0; x < image.width; ++x) {
                        const auto p = pixel(image, x, y);
                        teal += p[1] > p[0] + 40 && p[2] > p[0] + 30;
                        pink += p[0] > p[1] + 40 && p[2] > p[1] + 20;
                        gold += p[0] > p[2] + 60 && p[1] > p[2] + 40;
                    }
                }
                CHECK(teal > 30 && pink > 20 && gold > 100);
                image.write_ppm(std::string(VSIM_BINARY_DIR) + "/renderer-test.ppm");
                CHECK(renderer.validation_errors() == 0);
            });
        }
        test("renderer can be recreated after complete device teardown", [] {
            Renderer renderer({128, 128, true, true});
            Scene scene;
            (void)scene.create(camera({0, 0, 5}));
            (void)scene.create(mesh({0, 0, 0}, {0.8F, 0.4F, 0.1F}));
            CHECK(renderer.draw(scene));
            CHECK(renderer.capture().rgba.size() == 128 * 128 * 4);
            CHECK(renderer.validation_errors() == 0);
        });
    } catch (const std::exception& error) {
        std::cerr << "Renderer setup failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
