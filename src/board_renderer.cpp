// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Iru Cai's Rich4 reverse engineering: asm/rich4.asm (0x40829d,
// 0x409a23, 0x40b93b) and asm/rich4_load_map.asm (0x407ac0).
#include "rich4/board_renderer.hpp"

#include "rich4/assets.hpp"
#include "rich4/terrain.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rich4::board {
namespace {

#include "board_projection_tables.inc"

int floor_divide_32(int value) {
    const int quotient = value / 32;
    return quotient - (value < 0 && value % 32 != 0 ? 1 : 0);
}

Point fractional_offset(Point position, std::uint8_t rotation) {
    const auto &basis = fractional_basis[rotation];
    const int x = static_cast<unsigned>(position.x) & 31U;
    const int y = static_cast<unsigned>(position.y) & 31U;
    return {floor_divide_32(basis[0] * x) + floor_divide_32(basis[2] * y),
            floor_divide_32(basis[1] * x) + floor_divide_32(basis[3] * y)};
}

void validate_camera(const Camera &camera) {
    if (camera.rotation > 7) {
        throw std::invalid_argument("board camera rotation must be 0 through 7");
    }
    const auto &viewport = camera.viewport;
    if (viewport.left < 0 || viewport.top < 0 || viewport.right <= viewport.left ||
        viewport.bottom <= viewport.top || viewport.right > 8192 || viewport.bottom > 8192) {
        throw std::invalid_argument("board viewport must be a positive rectangle within 8192 pixels");
    }
}

Point screen_center(const Camera &camera) {
    return {(camera.viewport.left + camera.viewport.right) / 2,
            (camera.viewport.top + camera.viewport.bottom) / 2};
}

Point grid_point(int x, int y, const Camera &camera) {
    return camera_grid[camera.rotation][static_cast<std::size_t>((y + 14) * 29 + x + 14)];
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw assets::AssetError("truncated board metadata word");
    }
    return static_cast<std::uint16_t>(bytes[offset] | (unsigned(bytes[offset + 1]) << 8));
}

int read_i16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    const auto value = read_u16(bytes, offset);
    return value >= 0x8000 ? int(value) - 0x10000 : int(value);
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(read_u16(bytes, offset)) | (std::uint32_t(read_u16(bytes, offset + 2)) << 16);
}

std::filesystem::path find_archive(const std::filesystem::path &directory, std::string name) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        auto candidate = entry.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (candidate == name && entry.is_regular_file()) {
            return entry.path();
        }
    }
    throw assets::AssetError("missing board archive: " + (directory / name).string());
}

struct Placement {
    Point position;
    std::uint16_t sprite{};
    std::uint8_t direction{};
    std::uint8_t owner{};
    std::uint8_t level{};
    std::uint8_t type{};
};

std::vector<Placement> read_placements(std::span<const std::uint8_t> bytes, std::size_t table) {
    constexpr std::array<std::size_t, 5> record_sizes{40, 52, 56, 52, 28};
    const auto count = read_u32(bytes, table * 8);
    const auto offset = read_u32(bytes, table * 8 + 4);
    const auto record_size = record_sizes.at(table);
    if (offset < 40 || offset > bytes.size() || count > 65535 ||
        std::uint64_t(count + 1) * record_size > bytes.size() - offset) {
        throw assets::AssetError("invalid board metadata table bounds");
    }
    std::vector<Placement> result(count + 1);
    for (std::size_t i = 1; i <= count; ++i) {
        const auto record = bytes.subspan(offset + i * record_size, record_size);
        auto &placement = result[i];
        placement.position = {read_i16(record, 0), read_i16(record, 2)};
        if (table == 0) {
            placement.sprite = read_u16(record, 0x22);
        } else if (table == 4) {
            placement.sprite = read_u16(record, 0x1a);
            placement.direction = record[0x18];
        } else {
            placement.direction = record[0x1b];
            placement.owner = record[table == 3 ? 0x18 : 0x19];
            placement.level = record[0x1a];
            placement.type = record[0x18];
            if (table == 3) {
                placement.sprite = read_u16(record, 0x20);
            }
        }
    }
    return result;
}

graphics::Rect effective_clip(const graphics::Image &image, const Camera &camera) {
    validate_camera(camera);
    if (image.width <= 0 || image.height <= 0 || image.width > 8192 || image.height > 8192 ||
        image.pixels.size() != std::size_t(image.width) * std::size_t(image.height)) {
        throw std::invalid_argument("invalid board destination framebuffer");
    }
    return {camera.viewport.left, camera.viewport.top, std::min(camera.viewport.right, image.width),
            std::min(camera.viewport.bottom, image.height)};
}

std::uint16_t owner_color(std::uint8_t owner, const Scene &scene) {
    if (owner == 0) {
        return 0;
    }
    for (const auto &player : scene.players) {
        if (player.slot == std::size_t(owner - 1)) {
            return player.color;
        }
    }
    constexpr std::array<std::uint16_t, 4> colors{0x7c00, 0x03e0, 0x001f, 0x7fe0};
    return colors[(owner - 1) % colors.size()];
}

std::uint8_t owner_character(std::uint8_t owner, const Scene &scene) {
    for (const auto &player : scene.players) {
        if (owner != 0 && player.slot == std::size_t(owner - 1)) {
            return player.character;
        }
    }
    return 0;
}

Placement with_override(Placement placement, std::size_t id, std::span<const PropertyState> overrides) {
    for (const auto &state : overrides) {
        if (state.id == id) {
            placement.owner = state.owner;
            placement.level = state.level;
            placement.type = state.type;
            break;
        }
    }
    return placement;
}

} // namespace

std::size_t player_resource(const PlayerSprite &player) {
    if (player.character >= 12 || player.traffic_method > 3) {
        throw std::invalid_argument("invalid board player sprite selection");
    }
    // State 2 (toss) has no injured artwork: the original never tosses with those flags.
    const unsigned action = player.tossing ? 2U : unsigned(player.walking);
    const unsigned pose = player.special_pose && !player.tossing ? unsigned(player.special_pose)
                          : player.injured                       ? 18U
                          : player.sleepwalking ? (player.tossing ? 2U : 16U + action)
                          : player.on_water     ? 13U + action
                                                : player.traffic_method * 3U + action;
    return 0x80U + player.character * 21U + pose;
}

std::optional<Point> project(Point world, const Camera &camera) {
    validate_camera(camera);
    const std::int64_t tile_x = std::int64_t(floor_divide_32(world.x)) - floor_divide_32(camera.center.x);
    const std::int64_t tile_y = std::int64_t(floor_divide_32(world.y)) - floor_divide_32(camera.center.y);
    if (tile_x < -14 || tile_x > 14 || tile_y < -14 || tile_y > 14) {
        return std::nullopt;
    }
    const auto cell = grid_point(int(tile_x), int(tile_y), camera);
    const auto center = screen_center(camera);
    const auto camera_fraction = fractional_offset(camera.center, camera.rotation);
    const auto world_fraction = fractional_offset(world, camera.rotation);
    return Point{center.x + cell.x + camera_fraction.x - world_fraction.x,
                 center.y + cell.y + camera_fraction.y - world_fraction.y};
}

std::optional<Point> unproject(Point screen, const Camera &camera) {
    validate_camera(camera);
    const auto &viewport = camera.viewport;
    if (screen.x < viewport.left || screen.x >= viewport.right || screen.y < viewport.top ||
        screen.y >= viewport.bottom)
        return std::nullopt;
    const int base_x = floor_divide_32(camera.center.x), base_y = floor_divide_32(camera.center.y);
    std::optional<Point> best;
    std::int64_t best_distance = 0;
    for (int dy = -14; dy <= 14; ++dy)
        for (int dx = -14; dx <= 14; ++dx) {
            const Point world{(base_x + dx) * 32 + 16, (base_y + dy) * 32 + 16};
            const auto projected = project(world, camera);
            if (!projected)
                continue;
            const std::int64_t ex = projected->x - screen.x, ey = projected->y - screen.y;
            const auto distance = ex * ex + ey * ey;
            if (!best || distance < best_distance) {
                best = world;
                best_distance = distance;
            }
        }
    return best;
}

struct Renderer::Impl {
    std::uint8_t map_id;
    assets::Archive map_archive;
    assets::Archive data_archive;
    terrain::Ground ground;
    std::array<std::vector<Placement>, 5> placements;

    struct SpriteResource {
        std::vector<std::uint8_t> bytes;
        assets::Graphics layout;
    };
    using ResourceKey = std::pair<bool, std::size_t>; // true selects DATA.MKF.
    using FrameKey = std::tuple<bool, std::size_t, std::size_t, int>;
    std::map<ResourceKey, SpriteResource> resources;
    std::map<FrameKey, graphics::Image> frames;

    Impl(const std::filesystem::path &directory, std::uint8_t selected_map)
        : map_id(selected_map), map_archive(find_archive(directory, "map.mkf")),
          data_archive(find_archive(directory, "data.mkf")),
          ground(terrain::parse_ground(map_archive.decode(selected_map * 2))) {
        const auto metadata = map_archive.decode(selected_map * 2 + 1);
        for (std::size_t table = 0; table < placements.size(); ++table) {
            placements[table] = read_placements(metadata, table);
        }
    }

    SpriteResource &resource(bool data, std::size_t index) {
        const ResourceKey key{data, index};
        if (const auto found = resources.find(key); found != resources.end()) {
            return found->second;
        }
        auto bytes = (data ? data_archive : map_archive).decode(index);
        auto layout = assets::parse_graphics(bytes);
        return resources.emplace(key, SpriteResource{std::move(bytes), std::move(layout)}).first->second;
    }

    const graphics::Image &frame(bool data, std::size_t index, std::size_t number, int accent = -1) {
        const FrameKey key{data, index, number, accent};
        if (const auto found = frames.find(key); found != frames.end()) {
            return found->second;
        }
        const auto &source = resource(data, index);
        if (number >= source.layout.entries.size()) {
            throw assets::AssetError("board sprite frame outside original resource");
        }
        auto image = graphics::decode_frame(source.bytes, number);
        // The original changes only SPR palette slot 255 for property ownership.
        // Preserve the immutable resource and transparent slot zero separately.
        if (accent >= 0 && source.layout.kind == assets::GraphicsKind::spr) {
            const auto &entry = source.layout.entries[number];
            for (std::size_t i = 0; i < image.pixels.size(); ++i) {
                if (source.bytes[entry.offset + i] == 255) {
                    image.pixels[i] = static_cast<std::uint16_t>(accent);
                }
            }
        }
        return frames.emplace(key, std::move(image)).first->second;
    }

    std::size_t facility_base() const {
        return map_id < 4 ? 0x57 : 0x68 + (map_id - 4) * 17;
    }

    std::size_t scenery_resource(std::uint16_t id) const {
        if (id < 5) {
            return 0x27 + map_id * 5 + id;
        }
        if (id == 5) {
            return 0x4f + map_id;
        }
        if (id < 23) {
            return facility_base() + id - 6;
        }
        if (id == 23) {
            return 0x19;
        }
        return 0x26 + id;
    }
};

Renderer::Renderer(const std::filesystem::path &directory, std::uint8_t map_id) {
    if (map_id > 7) {
        throw std::invalid_argument("global board map id must be 0 through 7");
    }
    impl_ = std::make_unique<Impl>(directory, map_id);
}
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer &&) noexcept = default;
Renderer &Renderer::operator=(Renderer &&) noexcept = default;

std::optional<Point> Renderer::node_position(std::uint32_t id) const {
    if (id == 0 || id >= impl_->placements[0].size()) {
        return std::nullopt;
    }
    return impl_->placements[0][id].position;
}

std::size_t Renderer::node_count() const noexcept {
    return impl_->placements[0].size() - 1;
}
std::uint8_t Renderer::map_id() const noexcept {
    return impl_->map_id;
}

RenderStats Renderer::render_ground(graphics::Image &destination, const Camera &camera) {
    const auto clip = effective_clip(destination, camera);
    RenderStats stats;
    if (clip.right <= clip.left || clip.bottom <= clip.top) {
        return stats;
    }
    const auto &ground = impl_->ground;
    for (int y = clip.top; y < clip.bottom; ++y) {
        std::fill(destination.pixels.begin() + (y * destination.width + clip.left),
                  destination.pixels.begin() + (y * destination.width + clip.right), ground.palette[0]);
    }
    const auto center = screen_center(camera);
    const auto fraction = fractional_offset(camera.center, camera.rotation);
    const int center_tile_x = floor_divide_32(camera.center.x);
    const int center_tile_y = floor_divide_32(camera.center.y);
    constexpr std::array<std::uint8_t, terrain::tile_pixels> outside{};
    for (const auto offset : ground_order[camera.rotation]) {
        if (offset.y == -128) {
            break;
        }
        const std::array<Point, 4> corners{
            grid_point(offset.x, offset.y, camera), grid_point(offset.x + 1, offset.y, camera),
            grid_point(offset.x + 1, offset.y + 1, camera), grid_point(offset.x, offset.y + 1, camera)};
        terrain::Quad quad;
        for (std::size_t i = 0; i < quad.size(); ++i) {
            quad[i] = {static_cast<std::int16_t>(corners[i].y + center.y + fraction.y),
                       static_cast<std::int16_t>(corners[i].x + center.x + fraction.x)};
        }
        const auto tile_x = std::int64_t(center_tile_x) + offset.x;
        const auto tile_y = std::int64_t(center_tile_y) + offset.y;
        std::span<const std::uint8_t, terrain::tile_pixels> texture = outside;
        if (tile_x >= 0 && tile_x < ground.width_tiles && tile_y >= 0 && tile_y < ground.height_tiles) {
            const auto tile_index = ground.tile_indices[std::size_t(tile_y * ground.width_tiles + tile_x)];
            texture = ground.tile(tile_index);
        }
        terrain::draw_tile(destination, texture, ground.palette, quad, clip);
        ++stats.ground_tiles;
    }
    for (std::size_t i = 1; i < impl_->placements[0].size(); ++i) {
        const auto &marker = impl_->placements[0][i];
        if (marker.sprite == 0) {
            continue;
        }
        if (const auto position = project(marker.position, camera)) {
            graphics::blit(destination, impl_->frame(false, 0x18, marker.sprite - 1), position->x,
                           position->y, clip);
            ++stats.road_markers;
        }
    }
    return stats;
}

RenderStats Renderer::render(graphics::Image &destination, const Scene &scene, const Camera &camera) {
    auto stats = render_ground(destination, camera);
    const auto clip = effective_clip(destination, camera);
    // 漲價卡 / 查封卡 streets (fcn_0040829d rich4.asm:2170, 2354): straight onto the ground, before
    // any sprite, every set pixel of map.mkf 26 frame (direction & 1) + (facility ? 2 : 0) is ORed
    // with the mode colour of 0x488ef0 (RGB555: mode 0 = 0x7c00 for 漲價, mode 1 = 0x003f for 查封).
    for (std::size_t table = 1; table <= 2; ++table) {
        for (const auto &state : table == 1 ? scene.lands : scene.facilities) {
            if (!state.temporary || state.id == 0 || state.id >= impl_->placements[table].size())
                continue;
            const auto &placement = impl_->placements[table][state.id];
            const auto position = project(placement.position, camera);
            if (!position)
                continue;
            const auto direction = std::size_t((8 - ((placement.direction + camera.rotation) & 7)) & 7);
            const auto &mask = impl_->frame(false, 0x1a, (direction & 1U) + (table == 2 ? 2U : 0U));
            const std::uint16_t tint = (state.temporary & 1U) ? 0x003f : 0x7c00;
            const int left = position->x - mask.origin_x, top = position->y - mask.origin_y;
            for (int y = std::max(top, clip.top); y < std::min(top + mask.height, clip.bottom); ++y)
                for (int x = std::max(left, clip.left); x < std::min(left + mask.width, clip.right); ++x) {
                    const auto source = std::size_t((y - top) * mask.width + (x - left));
                    if (!mask.opaque_pixels.empty() && mask.opaque_pixels[source])
                        destination.pixels[std::size_t(y * destination.width + x)] |= tint;
                }
        }
    }
    struct DrawCommand {
        Point anchor;
        int priority;
        const graphics::Image *image;
    };
    std::vector<DrawCommand> commands;
    const auto enqueue = [&](Point world, bool data, std::size_t resource, std::size_t frame,
                             int priority = 0, int accent = -1) {
        if (const auto position = project(world, camera)) {
            commands.push_back({*position, priority, &impl_->frame(data, resource, frame, accent)});
        }
    };

    for (const auto &player : scene.players) {
        if (player.character >= 12 || player.direction > 7 || player.traffic_method > 3) {
            throw std::invalid_argument("invalid board player sprite selection");
        }
        const std::size_t resource_index = player_resource(player);
        const auto &resource = impl_->resource(true, resource_index);
        const auto frames_per_direction = resource.layout.entries.size() / 8;
        if (frames_per_direction == 0) {
            throw assets::AssetError("player sprite has fewer than eight directional frames");
        }
        const auto direction = std::size_t((player.direction + 8 - camera.rotation) & 7);
        const auto frame = direction * frames_per_direction + player.animation_frame % frames_per_direction;
        enqueue(player.position, true, resource_index, frame, player.active ? 13 : 12);
        if (player.sleepwalking) // object slot 19 (Data 414) at the same anchor, drawn right after
            enqueue(player.position, true, 0x19eU,
                    player.animation_frame % std::max<std::size_t>(1, impl_->resource(true, 0x19eU).layout.entries.size()),
                    player.active ? 13 : 12);
    }
    for (const auto &actor : scene.special_players) {
        if (actor.slot < 4 || actor.slot > 8 || actor.direction > 7) {
            throw std::invalid_argument("invalid board special actor selection");
        }
        const auto pose = actor.sleepwalking ? (actor.walking ? 3U : 0U)
                          : actor.on_water   ? 2U
                          : actor.walking    ? 1U
                                             : 0U;
        const std::size_t index =
            actor.slot == 8 ? 0x209U + unsigned(actor.walking) : 0x16cU + actor.slot * 4U + pose;
        const auto count = impl_->resource(true, index).layout.entries.size() / 8;
        if (count == 0)
            throw assets::AssetError("special actor sprite has fewer than eight directions");
        const auto direction = std::size_t((actor.direction + 8 - camera.rotation) & 7);
        enqueue(actor.position, true, index, direction * count + actor.animation_frame % count,
                actor.active ? 13 : 12);
        if (actor.sleepwalking && actor.slot < 8) // the same ZZZ marker (rich4.asm:1623)
            enqueue(actor.position, true, 0x19eU,
                    actor.animation_frame % std::max<std::size_t>(1, impl_->resource(true, 0x19eU).layout.entries.size()),
                    actor.active ? 13 : 12);
    }
    for (const auto &object : scene.objects) {
        if (object.kind == 0 || object.kind > 20 || object.direction > 7) {
            throw std::invalid_argument("invalid board object sprite selection");
        }
        const std::size_t index = 0x18cU + object.kind - 1U;
        const auto count = impl_->resource(true, index).layout.entries.size();
        if (count == 0) {
            throw assets::AssetError("object sprite has no frames");
        }
        const auto direction = std::size_t((object.direction + 8 - camera.rotation) & 7);
        if (object.follow) {
            // rich4.asm:2003-2024 / 1706-1720: offset behind the carrier, frame (d + 4) & 7, priority 0.
            static constexpr std::array<Point, 8> near{{{-10, -22}, {-22, -10}, {-22, 10}, {-10, 22},
                                                        {10, 22}, {22, 10}, {22, -10}, {10, -22}}};
            static constexpr std::array<Point, 8> far{{{-18, -44}, {-44, -18}, {-44, 18}, {-18, 44},
                                                       {18, 44}, {44, 18}, {44, -18}, {18, -44}}};
            if (const auto position = project(object.position, camera)) {
                const auto offset = (object.follow == 2 ? far : near)[direction];
                commands.push_back({{position->x + offset.x, position->y + offset.y}, 0,
                                    &impl_->frame(true, index, ((direction + 4) & 7) % count)});
            }
            continue;
        }
        const auto frame = count >= 8 ? direction : object.animation_frame % count;
        enqueue(object.position, true, index, frame);
    }
    for (std::size_t table = 1; table <= 2; ++table) {
        const auto overrides = table == 1 ? scene.lands : scene.facilities;
        for (std::size_t id = 1; id < impl_->placements[table].size(); ++id) {
            const auto property = with_override(impl_->placements[table][id], id, overrides);
            if (!project(property.position, camera)) {
                continue;
            }
            const auto direction = std::size_t((8 - ((property.direction + camera.rotation) & 7)) & 7);
            if (property.level == 0) {
                if (property.owner != 0) {
                    enqueue(property.position, false, 0x19, owner_character(property.owner, scene));
                }
                continue;
            }
            std::size_t resource;
            if (table == 1) {
                if (property.level > 5) {
                    throw std::invalid_argument("land building level exceeds five");
                }
                resource =
                    property.type ? 0x4f + impl_->map_id : 0x27 + impl_->map_id * 5 + property.level - 1;
            } else {
                const auto base = impl_->facility_base();
                switch (property.type) {
                case 0:
                    resource = base;
                    break;
                case 1:
                    resource = base + property.level;
                    break;
                case 2:
                    resource = base + 5 + property.level;
                    break;
                case 3:
                    resource = base + 11;
                    break;
                case 4:
                    resource = base + 11 + property.level;
                    break;
                default:
                    throw std::invalid_argument("facility type exceeds four");
                }
                if (resource >= base + 17) {
                    throw std::invalid_argument("facility level exceeds original sprite range");
                }
            }
            enqueue(property.position, false, resource, direction, 0, owner_color(property.owner, scene));
        }
    }
    for (std::size_t table = 3; table <= 4; ++table) {
        for (std::size_t id = 1; id < impl_->placements[table].size(); ++id) {
            const auto item = table == 3 ? with_override(impl_->placements[table][id], id, scene.commercials)
                                         : impl_->placements[table][id];
            if (item.sprite == 0) {
                continue;
            }
            const auto direction = std::size_t((8 - ((item.direction + camera.rotation) & 7)) & 7);
            enqueue(item.position, false, impl_->scenery_resource(item.sprite), direction, 0,
                    table == 3 ? owner_color(item.owner, scene) : -1);
        }
    }
    // The original signed16 sort key is (screen_y << 4) | category. Its
    // insertion order is players, objects, lands, facilities, companies, scenery.
    // Stable ties remove the platform dependence of the original qsort.
    const auto depth_key = [](const DrawCommand &command) {
        const auto bits = ((static_cast<unsigned>(command.anchor.y) << 4) & 0xfff0U) |
                          static_cast<unsigned>(command.priority);
        return bits >= 0x8000U ? int(bits) - 0x10000 : int(bits);
    };
    std::stable_sort(commands.begin(), commands.end(), [&](const auto &first, const auto &second) {
        return depth_key(first) < depth_key(second);
    });
    for (const auto &command : commands) {
        graphics::blit(destination, *command.image, command.anchor.x, command.anchor.y, clip);
        ++stats.sprites;
    }
    return stats;
}

} // namespace rich4::board
