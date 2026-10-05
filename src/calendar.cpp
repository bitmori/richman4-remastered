// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from mytbk/rich4 asm/rich4_date_utils.asm; see migration 004.
#include "rich4/calendar.hpp"
#include <array>
#include <stdexcept>

namespace rich4::calendar {
namespace {
constexpr std::array<std::uint8_t, 12> normal_month_days{31,28,31,30,31,30,31,31,30,31,30,31};
constexpr std::int32_t epoch_year = 1998;
constexpr std::int32_t maximum_year = 65535;

constexpr std::int32_t days_before_year(std::int32_t year) {
    return (year - epoch_year) * 365 + (year - 1) / 4 - (epoch_year - 1) / 4;
}
constexpr auto maximum_day = days_before_year(maximum_year + 1) - 1;

void require_valid(Date date) {
    if (!is_valid(date)) {
        throw std::invalid_argument("Date is outside the valid Rich4 calendar domain");
    }
}
} // namespace

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month) {
    if (month < 1 || month > 12) {
        throw std::invalid_argument("Rich4 month must be between 1 and 12");
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return normal_month_days[month - 1];
}

bool is_valid(Date date) noexcept {
    if (date.year < epoch_year || date.month < 1 || date.month > 12 || date.day < 1) {
        return false;
    }
    const auto month_length = date.month == 2 && is_leap_year(date.year)
        ? 29 : normal_month_days[date.month - 1];
    return date.day <= month_length;
}

std::int32_t days_from_1998(Date date) {
    require_valid(date);
    auto days = days_before_year(date.year);
    for (std::uint8_t month = 1; month < date.month; ++month) {
        days += days_in_month(date.year, month);
    }
    return days + date.day - 1;
}

Date from_days(std::int32_t days) {
    if (days < 0 || days > maximum_day) {
        throw std::out_of_range("Day index is outside the Rich4 calendar domain");
    }
    // Every four-year group beginning in 1998 has lengths 365,365,366,365.
    // Whole-cycle reduction replaces the original year-by-year traversal.
    auto year = epoch_year + (days / 1461) * 4;
    auto remainder = days % 1461;
    for (;;) {
        const auto year_length = year % 4 == 0 ? 366 : 365;
        if (remainder < year_length) {
            break;
        }
        remainder -= year_length;
        ++year;
    }
    const auto date_year = static_cast<std::uint16_t>(year);
    std::uint8_t month = 1;
    while (remainder >= days_in_month(date_year, month)) {
        remainder -= days_in_month(date_year, month);
        ++month;
    }
    return {date_year, month, static_cast<std::uint8_t>(remainder + 1)};
}

std::uint8_t weekday(Date date) {
    return static_cast<std::uint8_t>((days_from_1998(date) + 4) % 7);
}

DayAdvance next_day(Date date) {
    require_valid(date);
    auto result = date;
    const auto changed = date.day == days_in_month(date.year, date.month);
    if (!changed) {
        ++result.day;
    } else {
        result.day = 1;
        if (date.month == 12) {
            if (date.year == maximum_year) {
                throw std::out_of_range("Advancing date exceeds the Rich4 calendar domain");
            }
            result.month = 1;
            ++result.year;
        } else {
            ++result.month;
        }
    }
    return {result, changed};
}

Date add_days(Date date, std::int32_t delta) {
    const auto index = static_cast<std::int64_t>(days_from_1998(date)) + delta;
    if (index < 0 || index > maximum_day) {
        throw std::out_of_range("Date addition exceeds the Rich4 calendar domain");
    }
    return from_days(static_cast<std::int32_t>(index));
}

std::int32_t days_between(Date first, Date second) {
    return days_from_1998(second) - days_from_1998(first);
}

std::uint32_t add_packed_interval(std::uint32_t packed_date, std::uint32_t packed_interval) noexcept {
    auto result = packed_date + packed_interval; // Required modulo-2^32 arithmetic.
    if ((packed_interval & 0xff00U) != 0 && (result & 0xff00U) > 0x0c00U) {
        result += 0xf400U;
    }
    return result;
}

} // namespace rich4::calendar
