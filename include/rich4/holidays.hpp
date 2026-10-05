// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/calendar.hpp"
#include <cstdint>
#include <optional>

namespace rich4::holidays {

inline constexpr calendar::Date lunar_first_date{1998, 1, 1};
inline constexpr calendar::Date lunar_last_date{2020, 12, 31};

// Original date-table data only. Outside its range returns nullopt; never reads
// adjacent executable data or substitutes a guessed modern lunar conversion.
[[nodiscard]] std::optional<calendar::Date> lunar_date(calendar::Date date);

// Original holiday tables, indexed by stage*4+map. Sunday is always closed.
// Beyond the lunar table only original fixed Gregorian rules and Sunday apply.
[[nodiscard]] bool is_closed(calendar::Date date, std::uint16_t map, std::uint16_t stage);
[[nodiscard]] calendar::Date loan_due_date(calendar::Date current, std::uint16_t map, std::uint16_t stage);

} // namespace rich4::holidays
