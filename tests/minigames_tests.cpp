// SPDX-License-Identifier: GPL-3.0-or-later
// Mini-game rules (docs/ui/mini-games.md): RNG accounting, the penguin's walk, scoring and timing.
#include "rich4/minigames.hpp"
#include "rich4/random.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace rich4::minigames;

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename G> void to_play(G &game) {
    for (int i = 0; i < 100 && game.stage() == Stage::intro; ++i)
        game.tick();
    require(game.stage() == Stage::ready, "intro must end in the READY/GO movie");
    game.tick(); // the movie is modal: ticks do nothing
    require(game.stage() == Stage::ready, "ticks must not run during READY/GO");
    game.begin_play();
    require(game.stage() == Stage::play, "begin_play starts the game");
}

template <typename G, typename F> void run(G &game, F &&input, int limit = 20000) {
    for (int i = 0; i < limit && game.stage() != Stage::done; ++i) {
        if (game.stage() == Stage::ready)
            game.begin_play();
        input(i);
        game.tick();
    }
    require(game.stage() == Stage::done, "game never finished");
}

std::vector<Box> catcher_boxes() {
    std::vector<Box> boxes(25, Box{76, 74, 38, 72});
    for (int i = 0; i < 5; ++i)
        boxes[std::size_t(i)] = {66, 72, 33, 71};
    return boxes;
}

void penguin_placement() {
    for (std::uint32_t seed : {1U, 7U, 12345U, 0xdeadbeefU}) {
        Penguin game(seed);
        require(game.draws() == 28, "treasure placement draws one rand() per treasure");
        std::array<int, 6> kinds{};
        for (int row = 0; row < 9; ++row)
            for (int col = 0; col < 9; ++col) {
                const auto kind = game.cell(col, row).flags & 0xfU;
                require(!kind || Penguin::valid(col, row), "treasure on an invalid cell");
                ++kinds[kind];
            }
        require(kinds[1] == 3 && kinds[2] == 12 && kinds[3] == 3 && kinds[4] == 9 && kinds[5] == 1,
                "treasure counts differ from {3,12,3,9,1}");
        require(game.scene().size() == 28, "preview shows one marker per treasure");
        // First pick: rand()*64 >> 15 over the free cells in row-major order.
        rich4::Random random(seed);
        const int pick = (int(random.next()) * 64) >> 15;
        int seen = 0;
        for (int row = 0; row < 9; ++row)
            for (int col = 0; col < 9; ++col)
                if (Penguin::valid(col, row) && seen++ == pick)
                    require(game.cell(col, row).flags & 0xfU, "first treasure not at the original pick");
    }
}

// Every valid target is reachable from every valid start without stepping on an invalid cell.
void penguin_walks() {
    std::vector<std::pair<int, int>> cells;
    for (int row = 0; row < 9; ++row)
        for (int col = 0; col < 9; ++col)
            if (Penguin::valid(col, row))
                cells.emplace_back(col, row);
    require(cells.size() == 64, "the grid has 64 valid cells");
    for (const auto &[tc, tr] : cells) {
        Penguin game(3);
        to_play(game);
        // First walk to (tc,tr) from the start, then to every other cell from there.
        for (const auto &[gc, gr] : std::vector<std::pair<int, int>>{{tc, tr}, {6, 2}, {1, 1}, {7, 7}, {0, 4}}) {
            if (game.stage() != Stage::play || !game.idle())
                break;
            if (game.position() == std::pair{gc, gr})
                continue;
            require(game.tap(gc, gr), "tap on a valid cell must start a walk");
            for (int i = 0; i < 200 && !game.idle() && game.stage() == Stage::play; ++i) {
                game.tick();
                const auto [c, r] = game.position();
                require(Penguin::valid(c, r), "penguin walked onto an invalid cell");
            }
            if (game.stage() == Stage::play && game.idle())
                require(game.position() == std::pair{gc, gr}, "penguin did not reach the tapped cell");
        }
    }
    Penguin game(3);
    to_play(game);
    require(!game.tap(4, 4), "the igloo cell is not a target");
    require(!game.tap(2, 6), "the current cell is not a target");
}

void penguin_scoring_and_end() {
    // Dig everywhere until the time runs out or a bomb ends the game; the score is the formula.
    for (std::uint32_t seed : {1U, 2U, 99U}) {
        Penguin game(seed);
        std::vector<std::pair<int, int>> targets;
        for (int row = 0; row < 9; ++row)
            for (int col = 0; col < 9; ++col)
                if (Penguin::valid(col, row) && (game.cell(col, row).flags & 0xfU) > 1)
                    targets.emplace_back(col, row); // a player who memorised the safe treasures
        std::size_t next = 0;
        int ticks = 0;
        run(game, [&](int) {
            ++ticks;
            if (game.idle() && next < targets.size())
                game.tap(targets[next].first, targets[next].second), ++next;
        });
        const auto &found = game.found();
        require(game.score() == found[5] * 20 + found[3] * 12 + found[4] * 8 + found[2] * 5, "penguin score formula");
        require(game.score() > 0 && game.score_shown(), "safe digging must score and show it");
        require(game.draws() == 28, "the penguin draws randomness only for the placement");
        require(ticks >= 150 + 10, "the game lasts at least the 15 s timer");
    }
    // Digging a bomb ends the game early.
    Penguin game(5);
    int bomb_col = -1, bomb_row = -1;
    for (int row = 0; row < 9; ++row)
        for (int col = 0; col < 9; ++col)
            if ((game.cell(col, row).flags & 0xfU) == 1 && bomb_col < 0 && std::pair{col, row} != std::pair{2, 6})
                bomb_col = col, bomb_row = row;
    to_play(game);
    require(game.tap(bomb_col, bomb_row), "walk to the bomb");
    int ticks = 0;
    bool blast = false;
    while (game.stage() != Stage::done && ticks < 400) {
        game.tick();
        ++ticks;
        for (const auto &cue : game.drain_cues())
            blast |= cue.op == Cue::Op::play && cue.effect == 15;
    }
    require(blast && ticks < 120, "a bomb must explode (Effect 15) and end the game early");
    require(game.score() == 0, "nothing else was dug");
}

void balloon_rules() {
    Balloon game(42);
    to_play(game);
    // Ticks consume one rand() per empty slot until balloons appear.
    const auto before = game.draws();
    game.tick();
    require(game.draws() >= before + 16, "every empty slot rolls rand()%1000 each tick");
    // Pop every balloon at its centre; the score can only grow by the original rules.
    int pops = 0;
    run(game, [&](int) {
        for (const auto &slot : game.slots())
            if (slot.x && !(slot.kind & 0xf0U) && slot.y < 380)
                pops += game.tap(slot.x, slot.y);
    });
    require(pops > 5, "tapping balloons pops them");
    require(game.score() >= 0, "score stays non-negative");
    // A tap far away pops nothing.
    Balloon idle(42);
    to_play(idle);
    for (int i = 0; i < 30; ++i)
        idle.tick();
    require(idle.tap(-500, -500) == 0, "a miss pops nothing");
    // Determinism: same seed and same taps give the same draws and score.
    Balloon a(9), b(9);
    const auto play = [](Balloon &g) {
        run(g, [&](int i) {
            if (i % 3 == 0)
                for (const auto &slot : g.slots())
                    if (slot.x && !(slot.kind & 0xf0U)) {
                        g.tap(slot.x, slot.y);
                        break;
                    }
        });
    };
    play(a);
    play(b);
    require(a.score() == b.score() && a.draws() == b.draws() && a.random_state() == b.random_state(),
            "balloon game must be deterministic");
    rich4::Random replay(9);
    for (std::uint32_t i = 0; i < a.draws(); ++i)
        (void)replay.next();
    require(replay.state() == a.random_state(), "draw count reproduces the stream");
}

void catcher_rules() {
    require(Catcher::drift(320) == 0 && Catcher::drift(530) == 50 && Catcher::drift(110) == -50 &&
                Catcher::drift(362) == 10 && Catcher::drift(278) == -10,
            "drift = trunc((x-320)/210*260 - (x-320))");
    Catcher game(77, 4, catcher_boxes());
    require(game.god_x() == 110, "the 財神 starts at x 110");
    to_play(game);
    // Follow every falling treasure; avoid bombs.
    run(game, [&](int) {
        int best = -1, best_y = -1000;
        for (const auto &slot : game.slots())
            if (slot.x && (slot.bits & 0xf) != 4 && slot.y > best_y)
                best = slot.x, best_y = slot.y;
        if (best >= 0)
            game.set_target(best);
    });
    const auto &caught = game.caught();
    require(game.score() == caught[0] * 10 + caught[1] * 5 + caught[2] * 3 + caught[3], "catcher score formula");
    require(game.draws() > 10, "the 財神 and the goblin draw randomness");
    Catcher a(5, 0, catcher_boxes()), b(5, 0, catcher_boxes());
    const auto play = [](Catcher &g) {
        run(g, [&](int i) { g.set_target(i % 80 < 40 ? 100 : 540); });
    };
    play(a);
    play(b);
    require(a.score() == b.score() && a.draws() == b.draws(), "catcher must be deterministic");
}

} // namespace

int main() {
    try {
        penguin_placement();
        penguin_walks();
        penguin_scoring_and_end();
        balloon_rules();
        catcher_rules();
        std::cout << "mini-game tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "mini-game tests: " << error.what() << '\n';
        return 1;
    }
}
