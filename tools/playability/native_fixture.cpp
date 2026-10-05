// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic interaction fixtures. This program never overwrites source assets.
#include "rich4/assets.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/direction.hpp"
#include "rich4/save.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace rich4;
constexpr std::uint32_t fixture_seed = 0x52494348;
std::vector<std::uint8_t> read(const fs::path &file) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read " + file.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path &file, std::span<const std::uint8_t> bytes) {
    if (fs::exists(file))
        throw std::runtime_error("Fixture output already exists: " + file.string());
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output)
        throw std::runtime_error("Cannot write " + file.string());
}
void write(const fs::path &file, const std::string &text) {
    write(file, std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
}
std::string quote(const std::string &text) {
    std::string result = "\"";
    for (auto c : text) {
        if (c == '"' || c == '\\')
            result += '\\';
        if (static_cast<unsigned char>(c) < 32)
            throw std::runtime_error("Control character in filename");
        result += c;
    }
    return result + '"';
}
fs::path asset(const fs::path &directory, std::string expected) {
    for (const auto &entry : fs::directory_iterator(directory)) {
        auto name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (name == expected)
            return entry.path();
    }
    throw std::runtime_error("Missing " + expected);
}
bool connected(const MapNode &node) {
    return std::any_of(node.neighbors.begin(), node.neighbors.end(), [](auto n) { return n != 0; });
}
bool visible(board::Point point, const board::Camera &camera) {
    const auto projected = board::project(point, camera);
    return projected && projected->x >= 12 && projected->x < 428 && projected->y >= 52 && projected->y < 468;
}
bool land(const World &w, unsigned code) {
    return code > 2000 && code < 4000 && code - 2000 < w.lands.size();
}
bool facility(const World &w, unsigned code) {
    return code > 4000 && code < 6000 && code - 4000 < w.facilities.size();
}
std::uint16_t choose_center(const World &w, bool facility_start) {
    std::uint16_t best = 0;
    int best_score = -1;
    for (std::size_t i = 1; i < w.nodes.size(); ++i) {
        const auto &node = w.nodes[i];
        if (!connected(node) || (node.flags & 0x80000000U) ||
            (facility_start ? !facility(w, node.type_and_index) : !land(w, node.type_and_index)))
            continue;
        const board::Camera camera{{node.x, node.y}, 0};
        int score = 0;
        unsigned peers = 0;
        for (std::size_t j = 1; j < w.lands.size(); ++j)
            if (visible({w.lands[j].x, w.lands[j].y}, camera))
                score += 3;
        for (std::size_t j = 1; j < w.facilities.size(); ++j)
            if (visible({w.facilities[j].x, w.facilities[j].y}, camera))
                score += 7;
        for (std::size_t j = 1; j < w.nodes.size(); ++j)
            if (connected(w.nodes[j]) && !(w.nodes[j].flags & 0x80000000U) &&
                visible({w.nodes[j].x, w.nodes[j].y}, camera))
                ++peers;
        if (peers >= 8 && score > best_score) {
            best_score = score;
            best = std::uint16_t(i);
        }
    }
    return best;
}
std::vector<std::uint16_t> neighborhood(const World &w, std::uint16_t center) {
    const auto &origin = w.nodes[center];
    const board::Camera camera{{origin.x, origin.y}, 0};
    std::vector<std::uint16_t> nodes;
    for (std::size_t i = 1; i < w.nodes.size(); ++i)
        if (i != center && connected(w.nodes[i]) && !(w.nodes[i].flags & 0x80000000U) &&
            visible({w.nodes[i].x, w.nodes[i].y}, camera))
            nodes.push_back(std::uint16_t(i));
    const auto distance = [&](unsigned n) {
        const auto dx = int(w.nodes[n].x) - origin.x, dy = int(w.nodes[n].y) - origin.y;
        return dx * dx + dy * dy;
    };
    std::stable_sort(nodes.begin(), nodes.end(), [&](auto a, auto b) { return distance(a) < distance(b); });
    return nodes;
}
void place(World &w, std::size_t slot, std::uint16_t id) {
    auto &player = w.players[slot].state;
    auto &node = w.nodes[id];
    player.node_id = id;
    player.x = std::bit_cast<std::uint16_t>(node.x);
    player.y = std::bit_cast<std::uint16_t>(node.y);
    player.last_node_id =
        *std::find_if(node.neighbors.begin(), node.neighbors.end(), [](auto n) { return n != 0; });
    const auto &previous = w.nodes.at(player.last_node_id);
    player.direction = direction::from_delta(node.x - previous.x, node.y - previous.y);
    node.flags |= 0x100U << slot;
}
void configure(World &w, std::uint16_t center, unsigned first_card, unsigned last_card) {
    if (w.players.size() != 4)
        throw std::runtime_error("Interaction fixtures require a four-player source");
    w.header.date = {15, 6, 1998};
    w.header.current_player = 0;
    w.header.human_player_count = 1;
    w.header.price_index = 1;
    w.news_effect_days = 0;
    w.camera_rotation = 0;
    w.lease_mode = 0;
    w.round_counter = 0;
    w.public_fund = 0;
    w.prison_roster.fill(0);
    w.hospital_roster.fill(0);
    w.lottery_tickets.fill(0);
    for (auto &node : w.nodes)
        node.flags &= ~0x00ffff00U;
    for (std::size_t i = 0; i < w.objects.size(); ++i) {
        auto &object = w.objects[i];
        object = {};
        // Empty pool slots retain their type: paired gods can respawn here.
        object.kind = std::uint8_t(i < 14 ? i + 1 : i < 16 ? 15 : i < 26 ? 16 : i < 36 ? 17 : 18);
    }
    for (auto &actor : w.special_actors) {
        actor = {};
        actor.inactive_state = 1;
    }
    const auto nearby = neighborhood(w, center);
    if (nearby.size() < 7)
        throw std::runtime_error("Fixture neighborhood too small");
    for (std::size_t slot = 0; slot < w.players.size(); ++slot) {
        auto &p = w.players[slot];
        p.status = {};
        p.status.card_strategy = 3;
        p.status.rescue_strategy = 2;
        p.status.init_cash_ratio = 50;
        p.status.flags_100 = slot ? 2 : 1;
        p.state.controller_flags = slot ? 2 : 1;
        p.state.traffic_method = 0;
        p.state.dice_count = 1;
        p.state.cash = slot ? 300000 : 900000;
        p.state.bank_balance = 1500000;
        p.state.loan = 0;
        p.state.special_finance = 0;
        p.state.points = 1000;
        p.state.cards = {};
        p.state.tool_amounts = {};
        p.stocks = {};
        place(w, slot, slot ? nearby[slot - 1] : center);
    }
    for (unsigned id = first_card; id <= last_card; ++id)
        w.players[0].state.cards[id - first_card] = std::uint8_t(id);
    for (unsigned i = 0; i < 13; ++i)
        w.players[0].state.tool_amounts[i] = 3;
    for (std::size_t slot = 1; slot < 4; ++slot) {
        w.players[slot].state.cards = {1, 10, 15, 18, 19, 20, 21, 30};
        w.players[slot].state.tool_amounts[4] = 2;
    }
    for (std::size_t i = 1; i < w.lands.size(); ++i) {
        auto &p = w.lands[i];
        p.economy.owner_code = std::uint8_t(i % 4 + 1);
        p.economy.level = std::uint8_t(i % 5 + 1);
        p.economy.chain_store = 0;
        p.temporary_state = 0;
        p.expired_date = 0;
    }
    for (std::size_t i = 1; i < w.facilities.size(); ++i) {
        auto &p = w.facilities[i];
        p.owner_code = std::uint8_t(i % 4 + 1);
        p.level = std::uint8_t(i % 4 + 1);
        p.facility_type = std::uint8_t(i % 5);
        if (p.facility_type == 0 || p.facility_type == 3)
            p.level = 1;
        p.temporary_state = 0;
        p.expired_date = 0;
    }
    const auto code = w.nodes[center].type_and_index;
    if (land(w, code)) {
        auto &p = w.lands[code - 2000];
        p.economy.owner_code = 2;
        p.economy.level = 2;
    } else {
        auto &p = w.facilities[code - 4000];
        p.owner_code = 1;
        p.facility_type = 1;
        p.level = 2;
    }
    // Teleporting a property needs a vacant property of the same kind.
    const board::Camera camera{{w.nodes[center].x, w.nodes[center].y}, 0};
    for (std::size_t i = w.lands.size(); i-- > 1;)
        if (2000 + i != code && visible({w.lands[i].x, w.lands[i].y}, camera)) {
            auto &p = w.lands[i];
            p.economy.owner_code = 0;
            p.economy.level = 0;
            break;
        }
    for (std::size_t i = w.facilities.size(); i-- > 1;)
        if (4000 + i != code && visible({w.facilities[i].x, w.facilities[i].y}, camera)) {
            auto &p = w.facilities[i];
            p.owner_code = 0;
            p.level = 0;
            p.facility_type = 0;
            break;
        }
    // Deterministic objects explicitly constructed for card/tool interaction.
    w.objects[0].kind = 1;
    w.objects[0].node_id = nearby[3];
    w.objects[0].remaining_days = 5;
    w.nodes[nearby[3]].flags |= 1U << 16U;
    w.objects[4].kind = 5;
    w.objects[4].attached_player = 1;
    w.objects[4].remaining_days = 5;
    w.players[0].status.god_info = 5;
    w.objects[16].kind = 16;
    w.objects[16].node_id = nearby[4];
    w.nodes[nearby[4]].flags |= 17U << 16U;
    w.objects[26].kind = 17;
    w.objects[26].node_id = nearby[5];
    w.nodes[nearby[5]].flags |= 27U << 16U;
    w.objects[36].kind = 18;
    w.objects[36].attached_player = 1;
    w.objects[36].remaining_days = 8;
    w.players[0].status.attached_bomb = 37;
    for (auto &stock : w.stocks)
        stock.suspension_days = 0;
    w.players[0].stocks[0] = {1000, w.stocks[0].float_bits[2]};
    w.players[1].stocks[1] = {2000, w.stocks[1].float_bits[2]};
    // These are synthetic inventories, not a valid draw from a finite deck.
    // Supply remains available to test transfers, gifts and purchases.
    w.card_supply.fill(10);
    w.tool_supply.fill(20);
}
std::vector<std::uint16_t> forward_nodes(const World &w, std::uint16_t current, std::uint16_t last) {
    std::vector<std::uint16_t> result;
    const auto &node = w.nodes.at(current);
    for (unsigned i = 0; i < node.neighbors.size(); ++i) {
        const auto next = node.neighbors[i];
        if (next && next != last && !(node.flags & (0x40000000U >> i)))
            result.push_back(next);
    }
    return result;
}
World bankruptcy_world(const World &source) {
    std::uint16_t ai_node = 0, ai_last = 0, human_node = 0;
    // Select an existing route, without rewriting the map or movement rules.
    // The human steps off its property, then the AI takes its sole forward exit
    // onto that property. Both have the original one-step tortoise status.
    for (std::size_t i = 1; i < source.nodes.size() && !ai_node; ++i) {
        if (source.nodes[i].flags & 0x80000000U)
            continue;
        for (const auto last : source.nodes[i].neighbors) {
            if (!last)
                continue;
            const auto exits = forward_nodes(source, std::uint16_t(i), last);
            if (exits.size() != 1)
                continue;
            const auto target = exits.front();
            const auto code = source.nodes.at(target).type_and_index;
            if (!land(source, code) && !facility(source, code))
                continue;
            const auto human_exits = forward_nodes(source, target, std::uint16_t(i));
            if (human_exits.size() != 1)
                continue;
            const auto human_landing = source.nodes.at(human_exits.front()).type_and_index;
            if (!land(source, human_landing) && !facility(source, human_landing))
                continue;
            ai_node = std::uint16_t(i);
            ai_last = last;
            human_node = target;
            break;
        }
    }
    if (!ai_node)
        throw std::runtime_error("No deterministic original route for bankruptcy fixture");
    const auto encoded = source.export_save();
    const auto save = SaveFile::decode(encoded);
    const auto regions = save.regions();
    const auto map_region = std::find_if(regions.begin(), regions.end(),
                                         [](const auto &region) { return region.name == "map_header"; });
    if (map_region == regions.end())
        throw std::runtime_error("Save map region is missing");
    NewGameOptions options;
    options.map_id = source.header.map_id;
    options.stage_id = source.header.stage_id;
    options.date = {15, 6, 1998};
    options.random_seed = fixture_seed;
    options.players = {{10, 1, 0, human_node}, {3, 2, 0, ai_node}};
    auto w = World::from_map_resource(
        std::span(encoded).subspan(map_region->offset, save.map_summary().byte_size), options);
    for (auto &node : w.nodes)
        node.flags &= ~0x00ffff00U;
    for (auto &object : w.objects) {
        const auto kind = object.kind;
        object = {};
        object.kind = kind;
    }
    for (auto &actor : w.special_actors) {
        actor = {};
        actor.inactive_state = 1;
    }
    for (std::size_t slot = 0; slot < 2; ++slot) {
        auto &p = w.players[slot];
        p.status = {};
        p.status.flags_100 = slot ? 2 : 1;
        p.status.days_tortoise_walking = 5;
        p.state.cash = slot ? 1 : 1000000;
        p.state.bank_balance = slot ? 0 : 2000000;
        p.state.loan = 0;
        p.state.special_finance = 0;
        p.state.cards = {};
        p.state.tool_amounts = {};
        p.stocks = {};
        place(w, slot, slot ? ai_node : human_node);
    }
    w.players[0].state.last_node_id = ai_node;
    w.players[1].state.last_node_id = ai_last;
    for (std::size_t i = 1; i < w.lands.size(); ++i) {
        auto &p = w.lands[i];
        p.economy.owner_code = 1;
        p.economy.level = 5;
        p.economy.chain_store = 0;
        p.economy.toll_by_level.fill(50000);
        p.temporary_state = 0;
        p.expired_date = 0;
    }
    for (std::size_t i = 1; i < w.facilities.size(); ++i) {
        auto &p = w.facilities[i];
        p.owner_code = 1;
        p.level = 5;
        p.facility_type = 4;
        p.toll_by_level.fill(50000);
        p.temporary_state = 0;
        p.expired_date = 0;
    }
    for (auto &p : w.commercials)
        p.owner_code = 0;
    return w;
}
template <class T> void array(std::ostream &out, const T &values) {
    out << '[';
    bool first = true;
    for (auto value : values) {
        if (!first)
            out << ',';
        out << unsigned(value);
        first = false;
    }
    out << ']';
}
void save_fixture(const fs::path &directory, const std::string &label, const World &w,
                  const fs::path &source) {
    const auto path = directory / (label + ".DAT");
    const auto encoded = w.export_save();
    const auto decoded = World::from_save(SaveFile::decode(encoded));
    if (decoded.export_save() != encoded)
        throw std::runtime_error("Synthetic fixture failed lossless roundtrip");
    write(path, encoded);
    write(path.string() + ".rng", "RICH4-RNG-1 " + std::to_string(fixture_seed) + "\n");
    std::ostringstream json;
    json << "{\n  \"schema\":1,\n  \"synthetic\":true,\n  \"purpose\":\"Native card/tool UI interaction "
            "validation\",\n  \"source\":"
         << quote(fs::absolute(source).string()) << ",\n  \"save\":" << quote(path.filename().string())
         << ",\n  \"seed\":" << fixture_seed << ",\n  \"map\":" << w.header.map_id
         << ",\n  \"stage\":" << w.header.stage_id << ",\n  \"date\":\"1998-06-15\",\n  \"players\":[";
    for (std::size_t i = 0; i < w.players.size(); ++i) {
        if (i)
            json << ',';
        const auto &p = w.players[i].state;
        json << "{\"slot\":" << i << ",\"node\":" << p.node_id << ",\"last_node\":" << p.last_node_id
             << ",\"x\":" << p.x << ",\"y\":" << p.y << ",\"cash\":" << p.cash
             << ",\"bank\":" << p.bank_balance << ",\"points\":" << p.points << ",\"cards\":";
        array(json, p.cards);
        json << ",\"tools\":";
        array(json, p.tool_amounts);
        json << '}';
    }
    json << ']';
    if (label == "bankruptcy-victory")
        json << ",\n  "
                "\"expected_endgame\":{\"winner_slot\":0,\"bankrupt_slot\":1,\"ai_cash\":1,\"ai_bank\":0,"
                "\"tortoise_days\":5,\"property_toll\":50000,\"route_modified\":false}";
    if (label == "player-trading") {
        json << ",\n  \"expected_trading\":{\"buyer_slot\":0,\"seller_slot\":1,\"offer_index\":0,"
             << "\"kind\":3,\"item\":5,\"price\":1000,\"buyer_tool_count\":3,\"seller_tool_count\":2,"
             << "\"buyer_maximum_tool_count\":9,\"post_cancel_buy\":true}";
    }
    json << ",\n  \"nearby_targets\":[";
    bool first = true;
    const auto &active = w.players[0].state;
    const board::Camera camera{{active.x, active.y}, 0};
    for (std::size_t i = 1; i < w.lands.size(); ++i) {
        const auto &p = w.lands[i];
        if (!visible({p.x, p.y}, camera))
            continue;
        if (!first)
            json << ',';
        first = false;
        json << "{\"code\":" << 2000 + i << ",\"owner\":" << unsigned(p.economy.owner_code)
             << ",\"level\":" << unsigned(p.economy.level) << ",\"type\":" << unsigned(p.economy.chain_store)
             << '}';
    }
    for (std::size_t i = 1; i < w.facilities.size(); ++i) {
        const auto &p = w.facilities[i];
        if (!visible({p.x, p.y}, camera))
            continue;
        if (!first)
            json << ',';
        first = false;
        json << "{\"code\":" << 4000 + i << ",\"owner\":" << unsigned(p.owner_code)
             << ",\"level\":" << unsigned(p.level) << ",\"type\":" << unsigned(p.facility_type) << '}';
    }
    json << "],\n  \"objects\":[";
    first = true;
    for (std::size_t i = 0; i < w.objects.size(); ++i) {
        const auto &o = w.objects[i];
        if (!o.node_id && !o.attached_player)
            continue;
        if (!first)
            json << ',';
        first = false;
        json << "{\"id\":" << i + 1 << ",\"kind\":" << unsigned(o.kind) << ",\"node\":" << o.node_id
             << ",\"attached_player\":" << unsigned(o.attached_player)
             << ",\"remaining\":" << unsigned(o.remaining_days) << '}';
    }
    json << "],\n  \"limitations\":[\"Synthetic inventory deliberately does not conserve shared "
            "supply\",\"Time machine needs an in-session human turn snapshot before use\",\"Interaction "
            "fixture is not evidence of original full-game progression\"]\n}\n";
    write(directory / (label + ".json"), json.str());
    std::cout << path.string() << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        fs::path assets = RICH4_DEFAULT_ASSETS, output, source_save;
        unsigned map = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << "rich4-native-fixture --output NEW_DIRECTORY [--assets GAME_DIRECTORY] [--map "
                             "0..7] [--from-save FILE]\n";
                return 0;
            }
            if (i + 1 == argc)
                throw std::invalid_argument("Missing value for " + key);
            const std::string value = argv[++i];
            if (key == "--assets")
                assets = value;
            else if (key == "--output")
                output = value;
            else if (key == "--map")
                map = unsigned(std::stoul(value));
            else if (key == "--from-save")
                source_save = value;
            else
                throw std::invalid_argument("Unknown option " + key);
        }
        if (output.empty() || map > 7)
            throw std::invalid_argument("A new --output directory and map 0..7 are required");
        const auto source = source_save.empty() ? asset(assets, "map.mkf") : source_save;
        const auto source_before = read(source);
        if (fs::exists(output))
            throw std::invalid_argument("Output directory must not already exist");
        const auto output_path = fs::weakly_canonical(output), assets_path = fs::weakly_canonical(assets);
        if (std::mismatch(assets_path.begin(), assets_path.end(), output_path.begin(), output_path.end())
                .first == assets_path.end())
            throw std::invalid_argument("Write fixtures outside the original asset directory");
        World original;
        if (source_save.empty()) {
            NewGameOptions options;
            options.map_id = std::uint16_t(map % 4);
            options.stage_id = std::uint16_t(map / 4);
            options.random_seed = fixture_seed;
            original = World::from_map_resource(assets::Archive(source_before).decode(map * 2 + 1), options);
        } else
            original = World::from_save(SaveFile::decode(source_before));
        auto center = choose_center(original, false);
        const auto facility_center = choose_center(original, true);
        // The second-stage fourth map contains facilities but no ordinary lands.
        if (!center)
            center = facility_center;
        if (!center)
            throw std::runtime_error("No suitable connected fixture neighborhood");
        const auto endgame = bankruptcy_world(original);
        fs::create_directories(output);
        for (const auto &spec : std::array<std::pair<const char *, unsigned>, 3>{
                 {{"cards-01-15", 1}, {"cards-16-30", 16}, {"tools-01-13", 1}}}) {
            auto world = original;
            configure(world, center, spec.second, spec.second + 14);
            save_fixture(output, spec.first, world, source);
        }
        if (facility_center) {
            auto world = original;
            configure(world, facility_center, 1, 15);
            save_fixture(output, "cards-facility", world, source);
        }
        save_fixture(output, "bankruptcy-victory", endgame, source);
        {
            auto world = original;
            configure(world, center, 1, 15);
            world.offers = {};
            // Offer posting does not reserve inventory in the original game. The
            // seller owns two motorcycles; the human has room below the cap of nine.
            world.players[0].state.tool_amounts[4] = 3;
            world.players[1].state.tool_amounts[4] = 2;
            world.offers[1][0] = {OfferKind::tool, 0, 5, 1000, 0, 0, 0};
            save_fixture(output, "player-trading", world, source);
        }
        if (read(source) != source_before)
            throw std::runtime_error("Original source changed during fixture generation");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
