// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/graphics.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace rich4::board {

struct Point {
    int x{}, y{};
    friend bool operator==(const Point &, const Point &) = default;
};

struct Camera {
    Point center;            // Original map coordinates: 32 world units per ground tile.
    std::uint8_t rotation{}; // The eight original views, numbered 0 through 7.
    graphics::Rect viewport{0, 40, 440, 480};
};

struct PlayerSprite {
    Point position;
    std::uint8_t character{};
    std::uint8_t direction{};
    std::uint8_t animation_frame{};
    std::uint8_t traffic_method{}; // 0 foot, 1 motorcycle, 2 car, 3 construction vehicle.
    std::uint16_t color{0x7c00};   // RGB555 property accent color.
    std::size_t slot{};            // Property owner 1 refers to slot 0, and so on.
    bool active{};                 // Original depth tie-break: active player is in front.
    bool walking{};
    bool on_water{}, sleepwalking{}, injured{};
    // Pre-roll toss pose (original movement state 2, rich4_player_utils.asm fcn_0040d7c4):
    // animation_frame then indexes the toss frames of the facing direction.
    bool tossing{};
    // Original pose index forced over stand/walk (not toss): 12 construction-vehicle demolish swing,
    // 19 hospital gown, 20 prison stripes (fcn_0040bf93). 0 = the normal pose.
    std::uint8_t special_pose{};
};

// DATA.MKF entry the original loads for this player's current pose
// (_rich4_update_player_sprite, rich4.asm ~5350): 0x80 + character * 21 + pose, where pose is
// traffic * 3 + {0 stand, 1 walk, 2 toss}, 13/14/15 boat stand/walk/toss, 16/17 sleepwalk
// (tossing on foot, +2), 18 injured, or special_pose (12, 19, 20) when set. A sleepwalking
// player or actor also gets the Data 414 "ZZZ" object sprite above it (fcn_0040829d).
[[nodiscard]] std::size_t player_resource(const PlayerSprite &player);

struct ObjectSprite {
    Point position;
    std::uint8_t kind{}; // Original object kind 1 through 20.
    std::uint8_t direction{};
    std::uint8_t animation_frame{};
};

struct SpecialPlayerSprite {
    Point position;
    std::uint8_t slot{}; // Original slots 4..7 and tool robot slot 8.
    std::uint8_t direction{};
    std::uint8_t animation_frame{};
    bool active{}, walking{}, on_water{}, sleepwalking{};
};

struct PropertyState {
    std::uint32_t id{};   // One-based index in the corresponding original DAT table.
    std::uint8_t owner{}; // Zero means unowned; otherwise player slot plus one.
    std::uint8_t level{};
    std::uint8_t type{}; // Land: chain-store flag. Facility: original facility type.
    // Land +0x17 / facility +0x1c tmp_state: high nibble days left, low nibble 0 漲價卡, 1 查封卡.
    // Nonzero tints the lot (map.mkf 26, fcn_0040829d). Zero for companies.
    std::uint8_t temporary{};
};

// Read-only render snapshot. Missing property overrides retain the map's state.
struct Scene {
    std::span<const PlayerSprite> players;
    std::span<const ObjectSprite> objects;
    std::span<const PropertyState> lands;
    std::span<const PropertyState> facilities;
    std::span<const SpecialPlayerSprite> special_players;
    std::span<const PropertyState> commercials;
};

struct RenderStats {
    std::size_t ground_tiles{};
    std::size_t road_markers{};
    std::size_t sprites{};
};

// Exact original lookup and fractional-coordinate rounding. Points outside the
// original 29-by-29 camera neighborhood return nullopt, as the game culls them.
[[nodiscard]] std::optional<Point> project(Point world, const Camera &camera);

// Inverse of project() at ground-tile resolution: the world centre (tile * 32 + 16) of the
// ground tile whose projected centre is nearest to `screen`, among the 29-by-29 tiles project()
// draws. nullopt when `screen` lies outside the viewport. Used for board hit-testing
// (the original's _rich4_get_instance_from_position reads an ID buffer of the same view).
[[nodiscard]] std::optional<Point> unproject(Point screen, const Camera &camera);

class Renderer {
  public:
    // map_id is global: stage * 4 + map, in [0, 7]. MAP.MKF and DATA.MKF are
    // resolved without filename-case assumptions. No original EXE is loaded.
    Renderer(const std::filesystem::path &asset_directory, std::uint8_t map_id);
    ~Renderer();
    Renderer(Renderer &&) noexcept;
    Renderer &operator=(Renderer &&) noexcept;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    [[nodiscard]] std::optional<Point> node_position(std::uint32_t id) const;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::uint8_t map_id() const noexcept;

    // Paints only viewport; the application owns the toolbar and side panel.
    // Lazily caches decoded original sprites. Use one renderer per render thread.
    RenderStats render(graphics::Image &destination, const Scene &scene, const Camera &camera);
    RenderStats render_ground(graphics::Image &destination, const Camera &camera);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rich4::board
