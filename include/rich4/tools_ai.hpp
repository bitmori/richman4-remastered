// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rich4::tools_ai {

struct Decision {
    unsigned tool = 0;
    std::uint16_t target = 0;
    std::uint16_t destination = 0; // Teleport source is self; destination is a road ID.
    friend bool operator==(const Decision &, const Decision &) = default;
};

struct Callbacks {
    // Exact 0x40a45c(-1) rendered-anchor OR buffer, scanned row then column.
    std::function<std::vector<std::uint16_t>(std::size_t actor)> nearby_instances;
    // 0x4216ab returns EDX unchanged for a non-owned property. A capture may
    // supply the register left by the 0x409b18 selection-buffer refresh. Native
    // callers use zero; see migration 025 for this isolated fidelity limit.
    std::function<std::uint32_t()> ownership_carry;
};

struct Result {
    bool supported = true;
    std::optional<Decision> decision;
    std::string message;
};

// One predicate including the original aggression gate. Does not consume a tool
// or change World. Random draws, including rejected path forks, are retained.
[[nodiscard]] Result evaluate(const World &, Random &, std::size_t actor, unsigned tool,
                              const Callbacks & = {});
// Original AI UI visit: inventory order, randomized start only above four tool
// types, at most four attempts (wrapping even when fewer types are held).
[[nodiscard]] Result choose(const World &, Random &, std::size_t actor, const Callbacks & = {});

} // namespace rich4::tools_ai
