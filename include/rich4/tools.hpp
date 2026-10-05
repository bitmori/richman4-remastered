// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/world.hpp"
#include "rich4/random.hpp"
#include <functional>
#include <string>

namespace rich4::tools {
struct Callbacks {
    std::function<void(std::size_t)> robot;
    std::function<void(std::size_t,unsigned)> remote_dice;
    std::function<bool()> restore_last_state;
    std::function<void(std::uint16_t)> upgrade;
    std::function<void(std::uint16_t,bool,std::size_t)> blast;
    std::function<void(std::size_t)> before_teleport;
};
struct Result { bool supported=true, applied=false; std::string message; };
// Original selection bitfields. source=0 first step, otherwise destination step
// of teleport; zero means the tool needs no map selection (dice uses 1..6).
std::uint32_t selection_flags(unsigned tool, std::uint16_t source=0);
// Target codes retain original encoding: roads1..1999, lands2001..3999,
// facilities4001..5999, actors0x8000|(1<<slot), objects0x8000|(id<<8).
// No target means cancel. Missing mandatory callbacks reject before consumption.
Result use(World&,Random&,std::size_t player,unsigned tool,std::uint16_t target=0,
           std::uint16_t destination=0,const Callbacks& callbacks={});
// Transport can also be put away without spending a tool (original0x447c00).
bool dismount(World&,std::size_t player);
} // namespace rich4::tools
