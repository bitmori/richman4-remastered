// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/holidays.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace rich4;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void boundaries() {
    require(holidays::lunar_date({1998, 1, 1}) == calendar::Date{1997, 12, 3}, "First original lunar table record");
    require(holidays::lunar_date({2020, 12, 31}) == calendar::Date{2020, 11, 17}, "Last original lunar table record");
    require(!holidays::lunar_date({2021, 1, 1}) && !holidays::lunar_date({2026, 10, 5}),
            "After2020 must not read adjacent non-calendar executable data");
    require(holidays::lunar_date({1998, 1, 28}) == calendar::Date{1998, 1, 1}, "Original lunar new year transition");
    require(holidays::is_closed({1998, 1, 28}, 0, 0) && holidays::is_closed({1998, 1, 29}, 0, 0) &&
            holidays::is_closed({1998, 1, 30}, 0, 0), "Original Taiwan lunar New Year closure sequence");
    require(holidays::is_closed({1998, 1, 1}, 0, 0) && !holidays::is_closed({1998, 1, 3}, 0, 0) &&
            holidays::is_closed({1998, 1, 4}, 0, 0), "Gregorian closure, Saturday open and Sunday closed");
    require(holidays::is_closed({2026, 10, 10}, 0, 0) && !holidays::is_closed({2026, 10, 9}, 0, 0) &&
            holidays::is_closed({2026, 10, 11}, 0, 0), "Modern fallback retains fixed dates and Sunday");
    require(!holidays::is_closed({2026, 2, 17}, 0, 0), "Fallback does not guess a lunar new year beyond original table");
    require(!holidays::is_closed({2026, 11, 26}, 3, 0), "Fallback scope deliberately excludes nth-weekday calculation");
    require(holidays::loan_due_date({2026, 10, 5}, 0, 0) == calendar::Date{2027, 1, 4},
            "Ninety-day due date landing on Sunday advances to Monday");
    require(holidays::loan_due_date({2026, 7, 12}, 0, 0) == calendar::Date{2026, 10, 12},
            "Due date skips original fixed holiday and following Sunday");
    bool rejected = false;
    try { (void)holidays::is_closed({2026, 10, 5}, 4, 0); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "No unchecked holiday-table indexing");
    rejected = false;
    try { (void)holidays::lunar_date({1997, 12, 31}); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Dates before original epoch are invalid, not negative table indices");
}
std::vector<std::uint32_t> row(const std::string& line, std::size_t columns) {
    auto text = line; std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream input(text); std::vector<std::uint32_t> values; std::uint32_t value;
    while (input >> value) values.push_back(value);
    require(input.eof() && values.size() == columns, "Malformed original holiday fixture row");
    return values;
}
void oracle(const std::filesystem::path& directory) {
    std::ifstream file(directory / "holidays.csv");
    require(bool(file), "Cannot read original holiday capture");
    std::string line; std::getline(file, line);
    require(line == "index,date,lunar,map,stage,closed" || line == "index,date,lunar,map,stage,closed\r", "Holiday capture header");
    std::size_t count = 0;
    while (std::getline(file, line)) {
        const auto values = row(line, 6);
        require(values[0] == count / 8 && values[3] == count % 4 && values[4] == (count % 8) / 4 && count < 8401 * 8,
                "Holiday capture must contain every sequential date/map exactly once");
        const auto date = calendar::from_days(static_cast<std::int32_t>(count / 8));
        require(values[1] == calendar::pack(date), "Holiday fixture date/index mismatch");
        const auto lunar = holidays::lunar_date(date);
        if (!lunar || calendar::pack(*lunar) != values[2] ||
            holidays::is_closed(date, static_cast<std::uint16_t>(values[3]), static_cast<std::uint16_t>(values[4])) != (values[5] != 0)) {
            throw std::runtime_error("Original holiday mismatch at row " + std::to_string(count) + " date " + std::to_string(values[1]));
        }
        ++count;
    }
    require(count == 8401 * 8, "Incomplete holiday capture cannot pass");
    file.close(); file.open(directory / "loans.csv");
    require(bool(file), "Cannot read original loan capture"); std::getline(file, line);
    require(line == "date,map,stage,due" || line == "date,map,stage,due\r", "Loan capture header");
    count = 0;
    while (std::getline(file, line)) {
        const auto values = row(line, 4);
        require(count < 2184 && values[1] == count % 4 && values[2] == (count % 8) / 4,
                "Loan capture must cover every map once per month");
        const calendar::Date current{static_cast<std::uint16_t>(1998 + count / 96),
            static_cast<std::uint8_t>(1 + (count / 8) % 12), 1};
        require(values[0] == calendar::pack(current), "Loan capture month order/coverage");
        if (calendar::pack(holidays::loan_due_date(current, static_cast<std::uint16_t>(values[1]),
                                                  static_cast<std::uint16_t>(values[2]))) != values[3])
            throw std::runtime_error("Original loan due mismatch at row " + std::to_string(count));
        ++count;
    }
    require(count == 2184, "Incomplete loan capture cannot pass");
    std::cout << "Matched67208 original holiday cases and2184 original loan dates\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        boundaries();
        if (argc == 3 && std::string(argv[1]) == "--oracle") oracle(argv[2]);
        else if (argc != 1) throw std::invalid_argument("Usage: holidays_tests [--oracle capture-directory]");
        std::cout << "Holiday tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
