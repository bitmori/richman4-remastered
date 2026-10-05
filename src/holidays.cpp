// SPDX-License-Identifier: GPL-3.0-or-later
// Scalar holiday/lunar tables from mytbk/rich4 asm/rich4.asm; migration022.
#include "rich4/holidays.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace rich4::holidays {
namespace {
struct LunarRun {
    std::uint16_t first_day;
    std::uint32_t first_date;
};
constexpr std::array<LunarRun, 285> lunar_runs{{
    {0, 0x07cd0c03U},    {27, 0x07ce0101U},   {57, 0x07ce0201U},   {86, 0x07ce0301U},   {115, 0x07ce0401U},
    {145, 0x07ce0501U},  {174, 0x07ce0501U},  {203, 0x07ce0601U},  {233, 0x07ce0701U},  {263, 0x07ce0801U},
    {292, 0x07ce0901U},  {322, 0x07ce0a01U},  {352, 0x07ce0b01U},  {381, 0x07ce0c01U},  {411, 0x07cf0101U},
    {441, 0x07cf0201U},  {470, 0x07cf0301U},  {499, 0x07cf0401U},  {529, 0x07cf0501U},  {558, 0x07cf0601U},
    {587, 0x07cf0701U},  {617, 0x07cf0801U},  {646, 0x07cf0901U},  {676, 0x07cf0a01U},  {706, 0x07cf0b01U},
    {736, 0x07cf0c01U},  {765, 0x07d00101U},  {795, 0x07d00201U},  {825, 0x07d00301U},  {854, 0x07d00401U},
    {883, 0x07d00501U},  {913, 0x07d00601U},  {942, 0x07d00701U},  {971, 0x07d00801U},  {1001, 0x07d00901U},
    {1030, 0x07d00a01U}, {1060, 0x07d00b01U}, {1090, 0x07d00c01U}, {1119, 0x07d10101U}, {1149, 0x07d10201U},
    {1179, 0x07d10301U}, {1208, 0x07d10401U}, {1238, 0x07d10401U}, {1267, 0x07d10501U}, {1297, 0x07d10601U},
    {1326, 0x07d10701U}, {1355, 0x07d10801U}, {1385, 0x07d10901U}, {1414, 0x07d10a01U}, {1444, 0x07d10b01U},
    {1473, 0x07d10c01U}, {1503, 0x07d20101U}, {1533, 0x07d20201U}, {1563, 0x07d20301U}, {1592, 0x07d20401U},
    {1622, 0x07d20501U}, {1651, 0x07d20601U}, {1681, 0x07d20701U}, {1710, 0x07d20801U}, {1739, 0x07d20901U},
    {1769, 0x07d20a01U}, {1798, 0x07d20b01U}, {1828, 0x07d20c01U}, {1857, 0x07d30101U}, {1887, 0x07d30201U},
    {1917, 0x07d30301U}, {1946, 0x07d30401U}, {1976, 0x07d30501U}, {2006, 0x07d30601U}, {2035, 0x07d30701U},
    {2065, 0x07d30801U}, {2094, 0x07d30901U}, {2123, 0x07d30a01U}, {2153, 0x07d30b01U}, {2182, 0x07d30c01U},
    {2212, 0x07d40101U}, {2241, 0x07d40201U}, {2271, 0x07d40201U}, {2300, 0x07d40301U}, {2330, 0x07d40401U},
    {2360, 0x07d40501U}, {2389, 0x07d40601U}, {2419, 0x07d40701U}, {2448, 0x07d40801U}, {2478, 0x07d40901U},
    {2507, 0x07d40a01U}, {2537, 0x07d40b01U}, {2566, 0x07d40c01U}, {2596, 0x07d50101U}, {2625, 0x07d50201U},
    {2655, 0x07d50301U}, {2684, 0x07d50401U}, {2714, 0x07d50501U}, {2743, 0x07d50601U}, {2773, 0x07d50701U},
    {2803, 0x07d50801U}, {2832, 0x07d50901U}, {2862, 0x07d50a01U}, {2891, 0x07d50b01U}, {2921, 0x07d50c01U},
    {2950, 0x07d60101U}, {2980, 0x07d60201U}, {3009, 0x07d60301U}, {3039, 0x07d60401U}, {3068, 0x07d60501U},
    {3098, 0x07d60601U}, {3127, 0x07d60701U}, {3157, 0x07d60701U}, {3186, 0x07d60801U}, {3216, 0x07d60901U},
    {3246, 0x07d60a01U}, {3275, 0x07d60b01U}, {3305, 0x07d60c01U}, {3335, 0x07d70101U}, {3364, 0x07d70201U},
    {3393, 0x07d70301U}, {3423, 0x07d70401U}, {3452, 0x07d70501U}, {3481, 0x07d70601U}, {3511, 0x07d70701U},
    {3540, 0x07d70801U}, {3570, 0x07d70901U}, {3600, 0x07d70a01U}, {3630, 0x07d70b01U}, {3659, 0x07d70c01U},
    {3689, 0x07d80101U}, {3719, 0x07d80201U}, {3748, 0x07d80301U}, {3777, 0x07d80401U}, {3807, 0x07d80501U},
    {3836, 0x07d80601U}, {3865, 0x07d80701U}, {3895, 0x07d80801U}, {3924, 0x07d80901U}, {3954, 0x07d80a01U},
    {3984, 0x07d80b01U}, {4013, 0x07d80c01U}, {4043, 0x07d90101U}, {4073, 0x07d90201U}, {4103, 0x07d90301U},
    {4132, 0x07d90401U}, {4161, 0x07d90501U}, {4191, 0x07d90501U}, {4220, 0x07d90601U}, {4249, 0x07d90701U},
    {4279, 0x07d90801U}, {4308, 0x07d90901U}, {4338, 0x07d90a01U}, {4367, 0x07d90b01U}, {4397, 0x07d90c01U},
    {4427, 0x07da0101U}, {4457, 0x07da0201U}, {4486, 0x07da0301U}, {4516, 0x07da0401U}, {4545, 0x07da0501U},
    {4575, 0x07da0601U}, {4604, 0x07da0701U}, {4633, 0x07da0801U}, {4663, 0x07da0901U}, {4692, 0x07da0a01U},
    {4722, 0x07da0b01U}, {4751, 0x07da0c01U}, {4781, 0x07db0101U}, {4811, 0x07db0201U}, {4840, 0x07db0301U},
    {4870, 0x07db0401U}, {4900, 0x07db0501U}, {4929, 0x07db0601U}, {4959, 0x07db0701U}, {4988, 0x07db0801U},
    {5017, 0x07db0901U}, {5047, 0x07db0a01U}, {5076, 0x07db0b01U}, {5106, 0x07db0c01U}, {5135, 0x07dc0101U},
    {5165, 0x07dc0201U}, {5194, 0x07dc0301U}, {5224, 0x07dc0401U}, {5254, 0x07dc0401U}, {5283, 0x07dc0501U},
    {5313, 0x07dc0601U}, {5342, 0x07dc0701U}, {5372, 0x07dc0801U}, {5401, 0x07dc0901U}, {5431, 0x07dc0a01U},
    {5460, 0x07dc0b01U}, {5490, 0x07dc0c01U}, {5519, 0x07dd0101U}, {5549, 0x07dd0201U}, {5578, 0x07dd0301U},
    {5608, 0x07dd0401U}, {5637, 0x07dd0501U}, {5667, 0x07dd0601U}, {5697, 0x07dd0701U}, {5726, 0x07dd0801U},
    {5756, 0x07dd0901U}, {5785, 0x07dd0a01U}, {5815, 0x07dd0b01U}, {5844, 0x07dd0c01U}, {5874, 0x07de0101U},
    {5903, 0x07de0201U}, {5933, 0x07de0301U}, {5962, 0x07de0401U}, {5992, 0x07de0501U}, {6021, 0x07de0601U},
    {6051, 0x07de0701U}, {6080, 0x07de0801U}, {6110, 0x07de0901U}, {6140, 0x07de0901U}, {6169, 0x07de0a01U},
    {6199, 0x07de0b01U}, {6228, 0x07de0c01U}, {6258, 0x07df0101U}, {6287, 0x07df0201U}, {6317, 0x07df0301U},
    {6346, 0x07df0401U}, {6375, 0x07df0501U}, {6405, 0x07df0601U}, {6434, 0x07df0701U}, {6464, 0x07df0801U},
    {6494, 0x07df0901U}, {6524, 0x07df0a01U}, {6553, 0x07df0b01U}, {6583, 0x07df0c01U}, {6612, 0x07e00101U},
    {6642, 0x07e00201U}, {6671, 0x07e00301U}, {6701, 0x07e00401U}, {6730, 0x07e00501U}, {6759, 0x07e00601U},
    {6789, 0x07e00701U}, {6818, 0x07e00801U}, {6848, 0x07e00901U}, {6878, 0x07e00a01U}, {6907, 0x07e00b01U},
    {6937, 0x07e00c01U}, {6967, 0x07e10101U}, {6996, 0x07e10201U}, {7026, 0x07e10301U}, {7055, 0x07e10401U},
    {7085, 0x07e10501U}, {7114, 0x07e10601U}, {7143, 0x07e10601U}, {7173, 0x07e10701U}, {7202, 0x07e10801U},
    {7232, 0x07e10901U}, {7261, 0x07e10a01U}, {7291, 0x07e10b01U}, {7321, 0x07e10c01U}, {7351, 0x07e20101U},
    {7380, 0x07e20201U}, {7410, 0x07e20301U}, {7439, 0x07e20401U}, {7469, 0x07e20501U}, {7498, 0x07e20601U},
    {7527, 0x07e20701U}, {7557, 0x07e20801U}, {7586, 0x07e20901U}, {7616, 0x07e20a01U}, {7645, 0x07e20b01U},
    {7675, 0x07e20c01U}, {7705, 0x07e30101U}, {7735, 0x07e30201U}, {7764, 0x07e30301U}, {7794, 0x07e30401U},
    {7823, 0x07e30501U}, {7853, 0x07e30601U}, {7882, 0x07e30701U}, {7911, 0x07e30801U}, {7941, 0x07e30901U},
    {7970, 0x07e30a01U}, {7999, 0x07e30b01U}, {8029, 0x07e30c01U}, {8059, 0x07e40101U}, {8088, 0x07e40201U},
    {8118, 0x07e40301U}, {8148, 0x07e40401U}, {8178, 0x07e40401U}, {8207, 0x07e40501U}, {8237, 0x07e40601U},
    {8266, 0x07e40701U}, {8295, 0x07e40801U}, {8325, 0x07e40901U}, {8354, 0x07e40a01U}, {8384, 0x07e40b01U},
}};
struct Rule {
    std::uint8_t flags, calendar, month, day_or_nth, weekday;
};
constexpr std::array<std::array<Rule, 24>, 8> rules{{
    {{
        {1, 0, 1, 1, 0},   {0, 0, 2, 14, 0},  {1, 0, 3, 29, 0},    {0, 0, 4, 1, 0},   {1, 0, 4, 4, 0},
        {1, 0, 4, 5, 0},   {1, 0, 5, 1, 0},   {0, 2, 5, 2, 0},     {0, 0, 8, 8, 0},   {1, 0, 9, 28, 0},
        {1, 0, 10, 10, 0}, {1, 0, 10, 25, 0}, {128, 0, 10, 31, 0}, {1, 0, 10, 31, 0}, {1, 0, 11, 12, 0},
        {1, 0, 12, 25, 0}, {1, 1, 12, 31, 0}, {1, 1, 1, 1, 0},     {1, 1, 1, 2, 0},   {1, 1, 1, 3, 0},
        {0, 1, 1, 15, 0},  {1, 1, 5, 5, 0},   {0, 1, 7, 15, 0},    {1, 1, 8, 15, 0},
    }},
    {{
        {1, 0, 1, 1, 0},   {0, 0, 2, 14, 0},  {1, 0, 3, 8, 0},  {1, 0, 4, 5, 0},  {1, 0, 5, 4, 0},
        {1, 0, 6, 1, 0},   {1, 0, 7, 1, 0},   {1, 0, 8, 1, 0},  {1, 0, 5, 1, 0},  {1, 0, 10, 1, 0},
        {0, 0, 12, 25, 0}, {1, 1, 12, 31, 0}, {1, 1, 1, 1, 0},  {1, 1, 1, 2, 0},  {1, 1, 1, 3, 0},
        {0, 1, 1, 15, 0},  {1, 1, 5, 5, 0},   {0, 1, 7, 15, 0}, {1, 1, 8, 15, 0}, {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
    }},
    {{
        {1, 0, 1, 1, 0},  {1, 0, 1, 2, 0},   {1, 0, 1, 3, 0},   {1, 0, 1, 15, 0},  {0, 0, 2, 3, 0},
        {0, 0, 2, 14, 0}, {1, 0, 2, 11, 0},  {0, 0, 3, 3, 0},   {1, 0, 3, 22, 0},  {1, 0, 4, 29, 0},
        {1, 0, 5, 3, 0},  {1, 0, 5, 5, 0},   {1, 0, 9, 15, 0},  {1, 0, 9, 23, 0},  {1, 0, 10, 10, 0},
        {1, 0, 11, 3, 0}, {1, 0, 11, 23, 0}, {1, 0, 12, 23, 0}, {0, 0, 12, 25, 0}, {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},
    }},
    {{
        {1, 0, 12, 31, 0}, {1, 0, 1, 1, 0},   {0, 2, 1, 3, 1},  {0, 0, 2, 2, 0},   {1, 0, 2, 12, 0},
        {0, 0, 2, 14, 0},  {1, 2, 2, 3, 1},   {0, 0, 3, 30, 0}, {0, 2, 5, 2, 0},   {0, 2, 5, 5, 1},
        {0, 0, 6, 14, 0},  {0, 0, 6, 15, 0},  {1, 0, 7, 4, 0},  {0, 2, 9, 1, 1},   {0, 0, 10, 12, 0},
        {0, 0, 10, 31, 0}, {0, 0, 11, 11, 0}, {1, 2, 11, 4, 4}, {1, 0, 12, 24, 0}, {1, 0, 12, 25, 0},
        {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
    }},
    {{
        {1, 0, 1, 1, 0},  {1, 0, 2, 5, 0},   {1, 0, 2, 27, 0},  {1, 0, 6, 30, 0}, {1, 0, 7, 20, 0},
        {1, 0, 8, 23, 0}, {1, 0, 11, 11, 0}, {1, 0, 12, 25, 0}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},
    }},
    {{
        {1, 1, 1, 1, 0},  {1, 1, 1, 2, 0}, {1, 1, 1, 3, 0},  {0, 1, 1, 7, 0},  {0, 1, 1, 15, 0},
        {0, 1, 1, 20, 0}, {1, 1, 5, 5, 0}, {0, 1, 7, 15, 0}, {1, 1, 8, 15, 0}, {1, 1, 12, 31, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
    }},
    {{
        {1, 0, 1, 1, 0},  {1, 0, 2, 24, 0},  {1, 0, 4, 5, 0},   {1, 0, 6, 10, 0}, {1, 0, 6, 11, 0},
        {1, 0, 6, 12, 0}, {1, 0, 6, 13, 0},  {1, 0, 6, 14, 0},  {1, 0, 6, 15, 0}, {1, 0, 6, 16, 0},
        {1, 0, 8, 19, 0}, {1, 0, 11, 30, 0}, {1, 0, 12, 25, 0}, {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},
    }},
    {{
        {1, 0, 12, 31, 0}, {1, 0, 1, 1, 0},   {0, 0, 2, 14, 0}, {0, 0, 3, 30, 0},  {0, 2, 5, 2, 0},
        {1, 0, 8, 15, 0},  {0, 0, 10, 31, 0}, {1, 2, 11, 4, 4}, {1, 0, 12, 24, 0}, {1, 0, 12, 25, 0},
        {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},   {0, 0, 0, 0, 0},  {0, 0, 0, 0, 0},
    }},
}};
constexpr std::int32_t last_lunar_index = 8400;
} // namespace

std::optional<calendar::Date> lunar_date(calendar::Date date) {
    const auto day = calendar::days_from_1998(date);
    if (day > last_lunar_index)
        return std::nullopt;
    const auto next =
        std::upper_bound(lunar_runs.begin(), lunar_runs.end(), day,
                         [](std::int32_t value, const LunarRun &run) { return value < run.first_day; });
    const auto &run = *std::prev(next);
    return calendar::unpack(run.first_date + static_cast<std::uint32_t>(day - run.first_day));
}

// fcn_004521f0: index of the first matching holiday-table row whose flags lack 0x80, else nullopt
// (the original returns -1). The row also selects the festival picture (festival_picture).
std::optional<std::size_t> festival_row(calendar::Date date, std::uint16_t map, std::uint16_t stage) {
    if (map > 3 || stage > 1)
        throw std::invalid_argument("Holiday map/stage is outside the original eight maps");
    (void)calendar::weekday(date); // Validates the date.
    const auto lunar = lunar_date(date);
    auto compared = calendar::pack(date) & 0xffffU;
    const auto &table = rules[stage * 4U + map];
    for (std::size_t row = 0; row < table.size(); ++row) {
        const auto &rule = table[row];
        std::uint32_t target = 0;
        if (rule.calendar == 0) {
            target = (std::uint32_t(rule.month) << 8U) | rule.day_or_nth;
        } else if (rule.calendar == 1) {
            if (!lunar)
                continue;
            // Original EDI retains this value for any following rule, including
            // a subsequent Gregorian/nth-weekday rule. Do not silently fix it.
            compared = calendar::pack(*lunar) & 0xffffU;
            target = (std::uint32_t(rule.month) << 8U) | rule.day_or_nth;
        } else {
            if (!lunar)
                continue; // Explicit out-of-table fixed-date-only fallback.
            const calendar::Date first{date.year, rule.month, 1};
            const auto first_weekday = calendar::weekday(first);
            // Exact 0x45232e: when requested weekday precedes month start it
            // replaces the weekday with7, rather than adding7 to that weekday.
            const auto base = rule.weekday < first_weekday ? 7 : rule.weekday;
            const auto day = base - first_weekday + 1 + (rule.day_or_nth - 1) * 7;
            if (day <= calendar::days_in_month(date.year, rule.month))
                target = (std::uint32_t(rule.month) << 8U) | static_cast<std::uint32_t>(day);
        }
        if (compared == target && (rule.flags & 0x80U) == 0)
            return row;
    }
    return std::nullopt;
}

// fcn_004523d5: Sunday, or a matched row with non-zero flags.
bool is_closed(calendar::Date date, std::uint16_t map, std::uint16_t stage) {
    if (map > 3 || stage > 1)
        throw std::invalid_argument("Holiday map/stage is outside the original eight maps");
    if (calendar::weekday(date) == 0) // Also validates the date.
        return true;
    const auto row = festival_row(date, map, stage);
    return row && rules[stage * 4U + map][*row].flags != 0;
}

std::optional<std::size_t> festival_picture(calendar::Date date, std::uint16_t map, std::uint16_t stage) {
    // ref_00475208: first Data.mkf picture of each stage*4+map table (Data 4..127).
    static constexpr std::array<std::uint16_t, 8> first_picture{4, 28, 47, 67, 87, 108, 95, 118};
    const auto row = festival_row(date, map, stage);
    if (!row)
        return std::nullopt;
    return std::size_t(first_picture[stage * 4U + map]) + *row;
}

calendar::Date loan_due_date(calendar::Date current, std::uint16_t map, std::uint16_t stage) {
    auto date = calendar::add_days(current, 90);
    while (is_closed(date, map, stage))
        date = calendar::next_day(date).date;
    return date;
}
} // namespace rich4::holidays
