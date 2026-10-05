// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/calendar.hpp"
#include <cstddef>
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
// fcn_004521f0: the first matching (non-0x80) row of the map's holiday table, festival or not.
[[nodiscard]] std::optional<std::size_t> festival_row(calendar::Date date, std::uint16_t map, std::uint16_t stage);
// Data.mkf entry of the raw 200x200 festival picture the calendar shows instead of the season
// picture (rich4.asm loc_00416b4c, ui_bank fcn_00433f24): ref_00475208[stage*4+map] + row.
[[nodiscard]] std::optional<std::size_t> festival_picture(calendar::Date date, std::uint16_t map,
                                                          std::uint16_t stage);
[[nodiscard]] bool is_closed(calendar::Date date, std::uint16_t map, std::uint16_t stage);
// Presentation only: the day-change celebration of fcn_00452444 for the matching table row:
// Data FLC + Effect sound (0 = none) played at (0,40), and the MIDI track (0 = none).
struct Celebration {
    std::uint16_t movie = 0, sound = 0, music = 0;
    bool operator==(const Celebration &) const = default;
};
[[nodiscard]] std::optional<Celebration> celebration(calendar::Date date, std::uint16_t map, std::uint16_t stage);
[[nodiscard]] calendar::Date loan_due_date(calendar::Date current, std::uint16_t map, std::uint16_t stage);

} // namespace rich4::holidays
