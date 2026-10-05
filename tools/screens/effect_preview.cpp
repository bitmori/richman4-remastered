// SPDX-License-Identifier: GPL-3.0-or-later
// Developer preview of the board effect player (effects.hpp) without SDL: builds a new-game
// world, fabricates one EffectEvent around player 0, runs the real EffectPlayer on a fixed
// clock and writes a strip of evenly spaced board frames (half scale) to OUTPUT.ppm.
//
//   rich4-effect-preview ASSETS KIND OUTPUT.ppm [ARG] [FRAMES] [FULL_FRAME_MS]
//   KIND: dice (ARG = faces, e.g. 352) | sparkle | build | throw (ARG = 16/17/18) | missile |
//         nuclear | explosion | demolish | monster | god (ARG = god kind) | demon | bankrupt |
//         ambulance | police | dogcar | dogfight | ufo | airplane (ARG = return: arrive) |
//         parachute (ARG = character) | news (ARG = 4/5/15/20/21) | card | itembox | coupon |
//         truck | fireworks | christmas | zzz (sleepwalker, no effect) | gown (ARG = 19/20)
#include "effects.hpp"
#include "rich4/direction.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace rich4;

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "Usage: rich4-effect-preview ASSETS KIND OUTPUT.ppm [ARG] [FRAMES]\n";
        return 2;
    }
    try {
        const std::filesystem::path assets = argv[1];
        const std::string kind = argv[2];
        const std::string argument = argc > 4 ? argv[4] : "";
        const int samples = argc > 5 ? std::max(1, std::atoi(argv[5])) : 8;
        native::Artwork artwork(assets);
        World after = World::from_map_resource(artwork.archive('m').decode(1), NewGameOptions{});
        board::Renderer renderer(assets, 0);
        auto &actor = after.players[0].state;
        if (!actor.x && !actor.y) { // A fresh world places players when the session starts.
            if (!actor.node_id)
                actor.node_id = static_cast<std::uint16_t>(after.nodes.size() / 3);
            actor.x = static_cast<std::uint16_t>(after.nodes.at(actor.node_id).x);
            actor.y = static_cast<std::uint16_t>(after.nodes.at(actor.node_id).y);
        }
        // Nearest land to player 0 is the target of property effects.
        std::size_t land = 1;
        long best = -1;
        for (std::size_t i = 1; i < after.lands.size(); ++i) {
            const long dx = after.lands[i].x - actor.x, dy = after.lands[i].y - actor.y;
            if (best < 0 || dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                land = i;
            }
        }
        { // Stand on the road node nearest to that land, as a player landing on it would.
            long nearest = -1;
            for (std::size_t i = 1; i < after.nodes.size(); ++i) {
                const long dx = after.nodes[i].x - after.lands[land].x, dy = after.nodes[i].y - after.lands[land].y;
                if (nearest < 0 || dx * dx + dy * dy < nearest) {
                    nearest = dx * dx + dy * dy;
                    actor.node_id = static_cast<std::uint16_t>(i);
                }
            }
            actor.x = static_cast<std::uint16_t>(after.nodes[actor.node_id].x);
            actor.y = static_cast<std::uint16_t>(after.nodes[actor.node_id].y);
        }
        const auto code = static_cast<std::uint16_t>(2000 + land);
        EffectEvent event{EffectKind::dice_roll, 0, actor.node_id, {actor.x, actor.y}};
        const EffectPoint plot{after.lands[land].x, after.lands[land].y};
        World before = after;
        auto set_level = [&](World &world, std::uint8_t level) {
            world.lands[land].economy.owner_code = 1;
            world.lands[land].economy.level = level;
        };
        if (kind == "dice") {
            for (const char c : argument.empty() ? std::string("352") : argument)
                event.dice.push_back(static_cast<std::uint8_t>(c - '0'));
        } else if (kind == "sparkle" || kind == "build") {
            event = {kind == "build" ? EffectKind::build_rise : EffectKind::top_floor_sparkle, 0, actor.node_id, plot};
            event.property = code;
            set_level(before, 4);
            set_level(after, 5);
        } else if (kind == "missile" || kind == "nuclear" || kind == "demolish" || kind == "monster") {
            event = {kind == "missile"   ? EffectKind::missile
                     : kind == "nuclear" ? EffectKind::nuclear_missile
                     : kind == "demolish" ? EffectKind::demolish
                                          : EffectKind::monster,
                     0, actor.node_id, plot};
            event.property = code;
            set_level(before, 4);
            set_level(after, kind == "demolish" ? 3 : 0);
        } else if (kind == "explosion" || kind == "bankrupt") {
            event.kind = kind == "explosion" ? EffectKind::explosion : EffectKind::bankrupt;
        } else if (kind == "god") {
            event.kind = EffectKind::god_appear;
            event.object_kind = static_cast<std::uint8_t>(argument.empty() ? 1 : std::stoi(argument));
        } else if (kind == "demon") {
            event = {EffectKind::demon_puff, 0, actor.node_id, plot};
            set_level(before, 3);
            set_level(after, 2);
        } else if (kind == "throw") {
            // Three road links ahead of the player.
            std::uint16_t node = actor.node_id;
            for (int i = 0; i < 3; ++i)
                for (const auto next : after.nodes[node].neighbors)
                    if (next && next != actor.last_node_id && next != node) {
                        node = next;
                        break;
                    }
            const auto object_kind = static_cast<std::uint8_t>(argument.empty() ? 17 : std::stoi(argument));
            auto &object = after.objects[object_kind == 16 ? 15 : object_kind == 17 ? 25 : 35];
            object.kind = object_kind;
            object.node_id = node;
            event = {EffectKind::object_throw, 0, node, {after.nodes[node].x, after.nodes[node].y},
                     EffectPoint{actor.x, actor.y}, object_kind};
        } else if (kind == "ambulance" || kind == "police") { // confined: moved off the board
            event = {kind == "police" ? EffectKind::police : EffectKind::ambulance, 0, actor.node_id,
                     {actor.x, actor.y}, EffectPoint{actor.x, actor.y}};
            (kind == "police" ? after.players[0].status.days_in_prison : after.players[0].status.days_in_hospital) = 3;
        } else if (kind == "dogcar" || kind == "dogfight") {
            event = {kind == "dogcar" ? EffectKind::dog_vehicle : EffectKind::dog_fight, 0, actor.node_id,
                     {actor.x, actor.y}};
            if (kind == "dogfight") {
                event.from = EffectPoint{actor.x, actor.y};
                after.players[0].status.days_in_hospital = 3;
            } else
                actor.traffic_method = 2;
        } else if (kind == "ufo" || kind == "airplane") {
            const bool back = argument == "return";
            event = {kind == "ufo" ? EffectKind::ufo : EffectKind::airplane, 0, actor.node_id, {actor.x, actor.y},
                     back ? std::nullopt : std::optional<EffectPoint>(EffectPoint{actor.x, actor.y}),
                     static_cast<std::uint8_t>(back)};
            (back ? before : after).players[0].status.days_disappearing = 3;
        } else if (kind == "parachute") {
            const auto character = static_cast<std::uint8_t>(argument.empty() ? actor.character : std::stoi(argument));
            actor.character = before.players[0].state.character = character;
            event = {EffectKind::parachute, 0, actor.node_id, {actor.x, actor.y}, std::nullopt, character};
        } else if (kind == "news") {
            event = {EffectKind::news_movie, 0, actor.node_id, plot, std::nullopt,
                     static_cast<std::uint8_t>(argument.empty() ? 4 : std::stoi(argument))};
            event.property = code;
            set_level(before, 3);
            set_level(after, 0);
        } else if (kind == "card") {
            event = {EffectKind::card_throw, 0, actor.node_id, plot, EffectPoint{actor.x, actor.y}};
        } else if (kind == "itembox" || kind == "coupon") {
            event = {kind == "coupon" ? EffectKind::coupon : EffectKind::item_box, 0, actor.node_id, {actor.x, actor.y}};
        } else if (kind == "truck") {
            event = {EffectKind::truck_demolish, 0, actor.node_id, plot, EffectPoint{actor.x, actor.y},
                     direction::from_delta(plot.x - actor.x, plot.y - actor.y)};
            event.property = code;
            event.previous_level = 3;
            actor.traffic_method = before.players[0].state.traffic_method = 3;
            set_level(before, 3);
            set_level(after, 0);
            after.lands[land].economy.owner_code = before.lands[land].economy.owner_code = 2;
        } else if (kind == "fireworks" || kind == "christmas") {
            event.kind = kind == "christmas" ? EffectKind::christmas : EffectKind::holiday_fireworks;
        } else if (kind == "zzz" || kind == "gown") { // static board: one-frame dummy effect below
            if (kind == "zzz")
                after.players[0].status.days_sleep_walking = 3;
            event = {EffectKind::coupon, 0, actor.node_id, {actor.x, actor.y}};
        } else
            throw std::invalid_argument("unknown effect kind " + kind);
        std::array<std::uint8_t, 4> costumes{};
        if (kind == "gown")
            costumes[0] = static_cast<std::uint8_t>(argument.empty() ? 19 : std::stoi(argument));
        before.players[0].state = after.players[0].state;

        native::EffectPlayer player(artwork);
        player.set_costumes(costumes);
        player.remember(before, &after, 0);
        player.enqueue({event}, &after);
        std::vector<graphics::Image> frames;
        std::vector<unsigned> times;
        constexpr unsigned step = 20;
        unsigned now = 0;
        const auto log_sound = [&](std::size_t index) { std::cout << "t=" << now << "ms sound Effect[" << index << "]\n"; };
        while (player.active() && now < 20000) {
            auto canvas = graphics::make_canvas(640, 480, 0);
            // The app's draw_board: live world, camera on the active player.
            renderer.render(canvas, native::make_scene(after, now, costumes).view(), {{actor.x, actor.y}, 0});
            player.draw(canvas, renderer, after, 0, now ? step : 0, now, log_sound);
            auto half = graphics::make_canvas(220, 220); // keep memory small: board viewport at half scale
            for (int y = 0; y < 220; ++y)
                for (int x = 0; x < 220; ++x)
                    half.pixels[static_cast<std::size_t>(y * 220 + x)] =
                        canvas.pixels[static_cast<std::size_t>((40 + y * 2) * 640 + x * 2)];
            frames.push_back(std::move(half));
            if (argc > 6 && now == static_cast<unsigned>(std::atoi(argv[6])) / step * step) // full 640x480 frame
                graphics::write_ppm(canvas, std::string(argv[3]) + "-full.ppm");
            times.push_back(now);
            now += step;
        }
        std::cout << kind << ": " << now << " ms total\n";
        const int cell = 220, columns = std::min<int>(samples, static_cast<int>(frames.size()));
        auto strip = graphics::make_canvas(columns * (cell + 4), cell + 4, 0x7fff);
        for (int c = 0; c < columns; ++c) {
            const auto index = columns == 1 ? 0 : static_cast<std::size_t>(c) * (frames.size() - 1) / (columns - 1);
            const auto &frame = frames[index];
            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x)
                    strip.pixels[static_cast<std::size_t>((y + 2) * strip.width + c * (cell + 4) + 2 + x)] =
                        frame.pixels[static_cast<std::size_t>(y * cell + x)];
            std::cout << "cell " << c << " = t " << times[index] << " ms\n";
        }
        graphics::write_ppm(strip, argv[3]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "rich4-effect-preview: " << error.what() << '\n';
        return 1;
    }
}
