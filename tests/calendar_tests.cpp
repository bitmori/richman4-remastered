// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/calendar.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace rich4::calendar;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Operation> void rejects(Operation operation, const char* message) {
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    catch (const std::out_of_range&) { return; }
    throw std::runtime_error(message);
}

void unit_tests() {
    constexpr Date epoch{1998,1,1};
    check(pack(epoch) == 0x07ce0101U && unpack(pack(epoch)) == epoch, "packed date byte order");
    check(days_from_1998(epoch) == 0 && from_days(0) == epoch, "calendar epoch");
    check(weekday(epoch) == 4, "epoch weekday");
    check(days_from_1998({2000,1,1}) == 730, "year accumulation");
    check(is_leap_year(2100) && days_in_month(2100,2) == 29, "original non-Gregorian leap rule");
    check(next_day({2100,2,28}).date == Date{2100,2,29}, "2100 leap day");
    check(!next_day({2100,2,28}).month_changed, "leap day month flag");
    check(next_day({2100,2,29}).date == Date{2100,3,1}, "post-leap date");
    check(next_day({2100,2,29}).month_changed, "new month flag");
    check(next_day({1999,12,31}).date == Date{2000,1,1}, "year rollover");
    check(add_days({2000,3,1}, -1) == Date{2000,2,29}, "negative day offset");
    check(days_between({1998,1,10},epoch) == -9, "signed day difference");
    check(add_packed_interval(pack({1998,12,31}),0x100U) == pack({1999,1,31}), "packed month carry");
    check(add_packed_interval(pack({1998,1,31}),0x100U) == pack({1998,2,31}), "packed day is not clamped");
    check(add_packed_interval(pack({1998,1,31}),1) == pack({1998,1,32}), "packed days are not normalized");
    check(add_packed_interval(0xffffffffU,1) == 0, "packed modulo-32-bit arithmetic");

    rejects([]{ (void)days_from_1998({1997,12,31}); }, "pre-epoch date accepted");
    rejects([]{ (void)days_from_1998({2001,2,29}); }, "invalid leap day accepted");
    rejects([]{ (void)days_in_month(2000,13); }, "invalid month accepted");
    rejects([]{ (void)days_in_month(2000,0); }, "zero month accepted");
    rejects([]{ (void)from_days(-1); }, "negative day index accepted");
    rejects([]{ (void)add_days({1998,1,1},-1); }, "addition crossed epoch");
    rejects([]{ (void)add_days({2000,1,1},std::numeric_limits<std::int32_t>::max()); }, "addition overflow");
    rejects([]{ (void)add_days({2000,1,1},std::numeric_limits<std::int32_t>::min()); }, "subtraction overflow");
    constexpr Date maximum{65535,12,31};
    check(from_days(days_from_1998(maximum)) == maximum, "maximum date round trip");
    rejects([&]{ (void)next_day(maximum); }, "year overflow accepted");
    rejects([&]{ (void)from_days(days_from_1998(maximum)+1); }, "unrepresentable year accepted");

    // Exercise every date in the early game range and sampled dates throughout
    // the supported domain. Oracle comparisons below supply independent truth.
    for (std::int32_t day=0; day<75000; ++day) {
        const auto date = from_days(day);
        check(days_from_1998(date) == day, "calendar round trip");
        check(next_day(date).date == from_days(day+1), "advance consistency");
    }
    for (std::int32_t day=0; day<=days_from_1998(maximum); day+=997) {
        check(days_from_1998(from_days(day)) == day, "long-range calendar round trip");
    }
}

std::size_t check_original_calendar(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open original calendar CSV");
    std::string line;
    std::getline(input,line);
    std::size_t count=0;
    while (std::getline(input,line)) {
        if (line.empty()) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line);
        std::array<std::int64_t,9> fields{};
        for(auto& field:fields) if(!(row>>field)) throw std::runtime_error("Malformed calendar golden row");
        const auto date=unpack(static_cast<std::uint32_t>(fields[1]));
        const auto next=next_day(date);
        const bool equal=days_from_1998(date)==fields[0]
            && pack(from_days(static_cast<std::int32_t>(fields[0])))==fields[1]
            && weekday(date)==fields[2]
            && days_in_month(date.year,date.month)==fields[3]
            && pack(next.date)==fields[4] && next.month_changed==static_cast<bool>(fields[5])
            && pack(add_days(date,400))==fields[6]
            && (fields[0]<400 || pack(add_days(date,-400))==fields[7])
            && days_between(date,add_days(date,400))==fields[8];
        if(!equal) throw std::runtime_error("Calendar oracle mismatch at row "+std::to_string(count+1));
        ++count;
    }
    check(count>0,"Empty calendar golden file");
    return count;
}

std::size_t check_original_packed(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open original packed-calendar CSV");
    std::string line;
    std::getline(input,line);
    std::size_t count=0;
    while(std::getline(input,line)) {
        if(line.empty()) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line);
        std::uint32_t date,delta,result;
        if(!(row>>date>>delta>>result)) throw std::runtime_error("Malformed packed-calendar row");
        if(add_packed_interval(date,delta)!=result)
            throw std::runtime_error("Packed-calendar oracle mismatch at row "+std::to_string(count+1));
        ++count;
    }
    check(count>0,"Empty packed-calendar golden file");
    return count;
}
} // namespace

int main(int argc,char**argv) {
    try {
        unit_tests();
        std::cout<<"Calendar unit tests passed\n";
        if(argc>1) std::cout<<check_original_calendar(argv[1])<<" original calendar rows matched\n";
        if(argc>2) std::cout<<check_original_packed(argv[2])<<" original packed-calendar rows matched\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Calendar test failed: "<<error.what()<<'\n';
        return 1;
    }
}
