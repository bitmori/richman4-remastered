// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rich4::calendar {

struct Date {
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
    bool operator==(const Date &) const = default;
};

struct DayAdvance {
    Date date;
    bool month_changed;
};

// The game uses year % 4, including 2100. This is deliberately not Gregorian.
[[nodiscard]] constexpr bool is_leap_year(std::uint16_t year) noexcept {
    return year % 4 == 0;
}
[[nodiscard]] std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month);
[[nodiscard]] bool is_valid(Date date) noexcept;

// Explicit serialization of the original YYYYYYYY YYYYYYYY MMMMMMMM DDDDDDDD
// encoding. Pack/unpack preserve fields; calendar operations validate them.
[[nodiscard]] constexpr std::uint32_t pack(Date date) noexcept {
    return (static_cast<std::uint32_t>(date.year) << 16U) | (static_cast<std::uint32_t>(date.month) << 8U) |
           date.day;
}
[[nodiscard]] constexpr Date unpack(std::uint32_t packed) noexcept {
    return {static_cast<std::uint16_t>(packed >> 16U), static_cast<std::uint8_t>(packed >> 8U),
            static_cast<std::uint8_t>(packed)};
}

// Valid date domain: 1998-01-01 through 65535-12-31 in the original calendar.
// Invalid dates throw invalid_argument; out-of-domain arithmetic throws out_of_range.
[[nodiscard]] std::int32_t days_from_1998(Date date);
[[nodiscard]] Date from_days(std::int32_t days);
[[nodiscard]] std::uint8_t weekday(Date date); // Sunday = 0; epoch Thursday = 4.
[[nodiscard]] DayAdvance next_day(Date date);
[[nodiscard]] Date add_days(Date date, std::int32_t delta);
[[nodiscard]] std::int32_t days_between(Date first, Date second); // second - first.

// Legacy event expiry arithmetic (0x4521cb): a packed integer addition followed
// by a single month carry. Deliberately does not clamp days or normalize dates.
[[nodiscard]] std::uint32_t add_packed_interval(std::uint32_t packed_date,
                                                std::uint32_t packed_interval) noexcept;

} // namespace rich4::calendar
