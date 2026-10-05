// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/assets.hpp"
#include "rich4/board_renderer.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void requires_error(Function&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid board input did not fail");
}

void projection_tests() {
    using namespace rich4::board;
    Camera camera{{0, 0}};
    require(project({0, 0}, camera) == Point{220, 260}, "camera anchor changed");
    require(project({32, 0}, camera) == Point{254, 249}, "original east lookup changed");
    require(project({0, 32}, camera) == Point{234, 285}, "original south lookup changed");
    require(project({-32, 0}, camera) == Point{183, 269}, "negative tile rounding changed");
    require(project({1, 0}, camera) == Point{222, 260}, "fractional signed floor changed");
    require(project({31, 0}, camera) == Point{253, 250}, "fractional endpoint changed");
    require(project({-14 * 32, -14 * 32}, camera) == Point{-494, 49},
            "original camera table corner changed");
    require(!project({15 * 32, 0}, camera), "horizontal camera culling changed");
    require(!project({0, -15 * 32}, camera), "vertical camera culling changed");
    require(!project({std::numeric_limits<int>::max(), 0}, camera), "extreme point was not culled");
    camera.center = {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    require(project(camera.center, camera) == Point{220, 260}, "extreme camera arithmetic failed");
    for (int view = 0; view < 8; ++view) {
        camera.rotation = static_cast<std::uint8_t>(view);
        for (int fraction = 0; fraction < 32; ++fraction) {
            camera.center = {1024 + fraction, 1024 + (31 - fraction)};
            require(project(camera.center, camera) == Point{220, 260},
                    "active player does not remain at camera center");
        }
    }
    camera.rotation = 8;
    requires_error([&] { (void)project({0, 0}, camera); });
    requires_error([] { Renderer renderer("does-not-exist", 8); });
}

std::size_t compare_projection_golden(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read projection golden CSV");
    }
    std::string row;
    std::getline(input, row);
    std::size_t count = 0;
    while (std::getline(input, row)) {
        std::replace(row.begin(), row.end(), ',', ' ');
        std::istringstream fields(row);
        int center_x, center_y, rotation, world_x, world_y, screen_x, screen_y;
        if (!(fields >> center_x >> center_y >> rotation >> world_x >> world_y >> screen_x >>
              screen_y)) {
            throw std::runtime_error("malformed projection golden CSV row");
        }
        const rich4::board::Camera camera{{center_x, center_y},
                                          static_cast<std::uint8_t>(rotation)};
        const auto actual =
            rich4::board::project({world_x, world_y}, camera).value_or(rich4::board::Point{0, 0});
        require(actual == rich4::board::Point{screen_x, screen_y},
                "original EXE projection mismatch");
        ++count;
    }
    require(count != 0, "projection golden CSV is empty");
    return count;
}

void asset_tests(const std::filesystem::path& assets, const std::filesystem::path& output) {
    using namespace rich4;
    std::filesystem::create_directories(output);
    // Put two actors on an otherwise empty, off-map camera. Their shared anchor
    // makes depth ties observable: the active actor must cover the inactive one
    // even when the active actor was inserted first. Expected art is decoded
    // independently, then composited using the previously oracle-tested blitter.
    {
        board::Renderer renderer(assets, 0);
        const board::Camera camera{{-4096, -4096}};
        std::array<board::PlayerSprite, 2> players{};
        players[0].position = players[1].position = camera.center;
        players[0].character = 0;
        players[0].active = true;
        players[1].character = 10;
        players[1].slot = 1;
        const board::Scene scene{players, {}, {}, {}};
        auto expected = graphics::make_canvas(640, 480, 0x1234);
        renderer.render(expected, {}, camera);
        assets::Archive data(assets / "Data.mkf");
        const auto inactive = graphics::decode_frame(data.decode(0x80 + 10 * 21), 0);
        const auto active = graphics::decode_frame(data.decode(0x80), 0);
        graphics::blit(expected, inactive, 220, 260, camera.viewport);
        graphics::blit(expected, active, 220, 260, camera.viewport);
        auto actual = graphics::make_canvas(640, 480, 0x1234);
        renderer.render(actual, scene, camera);
        require(actual.pixels == expected.pixels,
                "actor asset selection, anchor, or depth order changed");
    }
    std::size_t renders = 0;
    std::size_t total_sprites = 0;
    for (std::uint8_t map = 0; map < 8; ++map) {
        board::Renderer renderer(assets, map);
        require(renderer.node_count() > 50, "original board has too few road nodes");
        require(!renderer.node_position(0), "node zero must remain the sentinel");
        require(!renderer.node_position(65535), "invalid node id was accepted");
        const auto center = renderer.node_position(51).value();
        const auto neighbor = renderer.node_position(50).value();
        std::array<board::PlayerSprite, 2> players{};
        players[0].position = center;
        players[0].character = 10;
        players[0].active = true;
        players[1].position = neighbor;
        players[1].character = 0;
        players[1].slot = 1;
        players[1].color = 0x03e0;
        std::array<board::PropertyState, 3> properties{{{1, 1, 1, 0}, {2, 2, 5, 0}, {3, 1, 0, 0}}};
        board::Scene scene{players, {}, properties, {}};
        const int rotations = map == 0 ? 8 : 1;
        for (int rotation = 0; rotation < rotations; ++rotation) {
            auto image = graphics::make_canvas(640, 480, 0x1234);
            board::Camera camera{center, static_cast<std::uint8_t>(rotation)};
            const auto stats = renderer.render(image, scene, camera);
            require(stats.ground_tiles > 100, "original tile traversal did not render the board");
            require(stats.sprites >= 2, "players were not rendered");
            require(image.pixels[0] == 0x1234 && image.pixels[479 * 640 + 639] == 0x1234,
                    "board drawing overwrote application chrome");
            require(std::count(image.pixels.begin(), image.pixels.end(), 0x1234) < 150000,
                    "board output is unexpectedly blank");
            const auto path = output / ("map-" + std::to_string(map) + "-view-" +
                                        std::to_string(rotation) + ".ppm");
            graphics::write_ppm(image, path);
            total_sprites += stats.sprites;
            ++renders;
        }
        auto image = graphics::make_canvas(640, 480);
        const board::Camera camera{center};
        const auto static_scene = renderer.render(image, {}, camera);
        require(static_scene.ground_tiles > 100, "empty scene did not preserve ground rendering");
        players[0].direction = 8;
        requires_error([&] { renderer.render(image, scene, camera); });
    }
    std::cout << renders << " original-asset board renders, " << total_sprites
              << " sprite draws; output " << output << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        projection_tests();
        if (argc >= 3) {
            asset_tests(argv[1], argv[2]);
        }
        if (argc >= 4) {
            std::cout << compare_projection_golden(argv[3])
                      << " original EXE projection rows matched\n";
        }
        std::cout << "board renderer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "board renderer tests failed: " << error.what() << '\n';
        return 1;
    }
}
