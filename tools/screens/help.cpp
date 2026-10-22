// SPDX-License-Identifier: GPL-3.0-or-later
// Help screen, see help.hpp. Coordinates are relative to the window origin (0x48c5e4/0x48c5e8)
// and come from fcn_0044dfb4 / fcn_0044dd9f / _rich4_ui_help_callback and the region table
// ref_00476254 of rich4_ui_help.asm.
#include "help.hpp"

#include "rich4/services.hpp"

#include <algorithm>
#include <exception>
#include <span>

namespace rich4::native::screens {
namespace {

struct Category {
    const char *name;                   // ref_004761b4
    std::vector<const char *> topics;   // ref_004761b8
    unsigned first_entry;               // ref_004761bc: help.mkf entry of topic 0
};

const std::array<Category, 8> &categories() {
    static const std::array<Category, 8> table{{
        {"操作說明", {"遊戲操作"}, 1},
        {"遊戲畫面", {"日、月曆", "地產資料", "其他資料", "物價指數", "股票資料", "資金資料"}, 2},
        {"遊戲指令",
         {"LOAD", "SAVE", "卡片", "交易", "地圖", "系統", "股市", "前進", "查詢", "託管", "道具", "說明"},
         8},
        {"房 地 產", {"公司企業", "住宅用地", "商業用地"}, 20},
        {"特殊地點",
         {"七彩氣球", "公園", "卡片", "企鵝挖寶", "百貨公司", "命運", "得十點", "得三十點", "得五十點", "喜從天降",
          "新聞", "監獄", "銀行", "樂透", "醫院", "魔法屋"},
         23},
        {"特殊人物",
         {"乞丐", "土地公", "大衰神", "大財神", "大福神", "大窮神", "小衰神", "小財神", "小偷", "小福神", "小窮神",
          "天使", "死神", "流氓", "強盜", "惡犬", "惡魔", "間諜"},
         39},
        {"卡  片",
         {"天使卡", "冬眠卡", "同盟卡", "免費卡", "免罪卡", "均貧卡", "均富卡", "改建卡", "怪獸卡", "拍賣卡",
          "拆除卡", "查封卡", "查稅卡", "紅卡",   "烏龜卡", "送神符", "停留卡", "陷害卡", "復仇卡", "惡魔卡",
          "換地卡", "換屋卡", "黑卡",   "嫁禍卡", "搶奪卡", "夢遊卡", "漲價卡", "請神符", "購地卡", "轉向卡"},
         57},
        {"道  具",
         {"工程車", "地雷", "汽車", "定時炸彈", "飛彈", "時光機", "核子飛彈", "傳送機", "路障", "遙控骰子", "機車",
          "機器工人", "機器娃娃"},
         87},
    }};
    return table;
}

constexpr int window_size = 400;
constexpr std::size_t lines_per_page = 14; // fcn_0044dd9f stops after line index 13
constexpr unsigned visible_topics = 8;
// rich4_create_font(12, 101010, 101010, 1): the original same-colour 1 px shadow only thickened a
// bitmap 細明體; with an outline font scaled 2-3x on a phone it reads as a blurry double image.
const Style text_style{12, 0x101010, 0x101010, 0};
const Style topic_style{15, 0x101010, 0x101010, bold};  // rich4_create_font(15, 101010, 101010, 2)

} // namespace

void HelpScreen::open(int x, int y) {
    open_ = true;
    x_ = x < 0 ? 320 - window_size / 2 : x; // fcn: 0x140 - width/2, 0xf0 - height/2
    y_ = y < 0 ? 240 - window_size / 2 : y;
    category_ = topic_ = 0;
    loaded_ = -1;
    page_ = 0;
}

void HelpScreen::cancel(Host &host) {
    host.sound(cancel_sound); // ref_00482332
    close();
}

void HelpScreen::select(Artwork &artwork, unsigned category, unsigned topic) {
    category_ = category;
    topic_ = topic;
    const int entry = int(categories()[category].first_entry + topic);
    if (entry == loaded_)
        return;
    loaded_ = entry;
    page_ = 0;
    pages_.assign(1, {});
    std::vector<std::uint8_t> bytes;
    try {
        bytes = artwork.archive('h').decode(std::size_t(entry));
    } catch (const std::exception &) {
        return;
    }
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < bytes.size();) {
        auto end = start;
        while (end < bytes.size() && bytes[end])
            ++end;
        lines.emplace_back(reinterpret_cast<const char *>(bytes.data() + start), end - start);
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    std::vector<std::string> text;
    for (const auto &line : lines)
        text.push_back(line == "@" ? line : ui::big5(line));
    if (entry == 58) // help.mkf 58 (冬眠卡) is a byte copy of 紅卡 in the shipped data; see items.cpp.
        text = {"功能：讓其他所有角色", "冬眠五天，期間無法行", "動。"};
    for (auto &line : text) {
        if (line == "@") { // a lone '@' line starts the next page
            pages_.emplace_back();
            continue;
        }
        if (pages_.back().size() == lines_per_page)
            pages_.emplace_back();
        pages_.back().push_back(std::move(line));
    }
    while (pages_.size() > 1 && pages_.back().empty())
        pages_.pop_back();
}

void HelpScreen::scroll(int direction) { // loc_0044e881 / loc_0044e944: by 8 topics, clamped
    const auto count = unsigned(categories()[category_].topics.size());
    if (count <= visible_topics)
        return;
    auto &first = first_[category_];
    const unsigned last_first = count - visible_topics;
    const unsigned target = direction < 0 ? (first > visible_topics ? first - visible_topics : 0)
                                          : std::min(first + visible_topics, last_first);
    topic_ = unsigned(std::clamp(int(topic_) + int(target) - int(first), 0, int(count) - 1));
    first = target;
}

void HelpScreen::draw(Host &host, const Give &give) {
    if (!open_)
        return;
    auto &frame = host.frame;
    if (loaded_ < 0)
        select(frame.artwork, category_, topic_);
    const int X = x_, Y = y_;
    const auto at = [X, Y](int left, int top, int right, int bottom) {
        return graphics::Rect{X + left, Y + top, X + right, Y + bottom};
    };
    const auto pressed = [&frame](graphics::Rect rect) { return frame.pointer_down && frame.pointer_in(rect); };
    const auto &category = categories()[category_];
    const auto count = unsigned(category.topics.size());

    // Window and category column (_rich4_ui_help_entry draws the 8 names into frame 0).
    frame.picture('h', 0, 0, X, Y);
    for (unsigned i = 0; i < 8; ++i)
        text(frame, categories()[i].name, X + 59, Y + 73 + 36 * int(i), 2, text_style);
    frame.picture('h', 0, 1, X + 26, Y + 58 + 36 * int(category_)); // fcn_0044dfb4: selected tab
    text(frame, category.name, X + 59, Y + 73 + 36 * int(category_), 2, text_style);
    text(frame, category.name, X + 140, Y + 57, 2, text_style); // red column caption

    // Topic list, 8 rows from first_[category].
    auto &first = first_[category_];
    first = std::min(first, count > visible_topics ? count - visible_topics : 0U);
    // ref_00476254 regions; touch builds grow them to finger size.
    const bool big = platform::touch_ui;
    const graphics::Rect up_hit = big ? at(146, 30, 204, 58) : at(170, 43, 192, 58),
                         down_hit = big ? at(146, 58, 204, 78) : at(170, 59, 192, 74);
    if (count > visible_topics) {
        frame.picture('h', 0, pressed(up_hit) ? 6 : 4, X + 170, Y + 43);
        frame.picture('h', 0, pressed(down_hit) ? 7 : 5, X + 170, Y + 59);
    }
    for (unsigned row = 0; row < visible_topics && first + row < count; ++row) {
        const unsigned index = first + row;
        frame.picture('h', 0, index == topic_ ? 3 : 2, X + 108, Y + 78 + 34 * int(row));
        text(frame, category.topics[index], X + 150, Y + 94 + 34 * int(row), 2, topic_style);
    }
    text(frame, category.topics.at(topic_), X + 270, Y + 63, 2, topic_style); // yellow pane caption

    // Text pane: (232, 90 + 18 * line), 14 lines per page.
    const auto &lines = pages_.at(std::min(page_, pages_.size() - 1));
    for (std::size_t i = 0; i < lines.size(); ++i)
        text(frame, lines[i], X + 232, Y + 90 + 18 * int(i), 0, text_style);
    const bool paged = pages_.size() > 1;
    const graphics::Rect prev_hit = big ? at(296, 30, 344, 86) : at(322, 48, 345, 80),
                         next_hit = big ? at(344, 30, 396, 86) : at(343, 48, 366, 80);
    if (paged) {
        frame.picture('h', 0, pressed(prev_hit) ? 10 : 8, X + 322, Y + 48);
        frame.picture('h', 0, pressed(next_hit) ? 11 : 9, X + 343, Y + 48);
    }

    // Port addition: "+1" gives the topic's card or tool. Top right of the yellow pane; the two
    // paged tool topics (定時炸彈, 傳送機) keep their page arrows there, so it moves to the bottom
    // right, beside their short last lines.
    const bool cards = category_ == 6, tools = category_ == 7;
    if (give && (cards || tools)) {
        const auto name = category.topics.at(topic_);
        const auto &table = cards ? std::span<const std::string_view>(services::card_names)
                                  : std::span<const std::string_view>(services::tool_names);
        const auto found = std::find(table.begin(), table.end(), std::string_view(name));
        if (found != table.end()) {
            const auto id = std::uint8_t(found - table.begin() + 1);
            const auto rect = paged ? at(322, 328, 360, 350) : at(322, 52, 360, 76);
            const bool down = pressed(rect);
            ui::fill(frame.canvas, rect, down ? ui::color(0xd8, 0xb8, 0x48) : ui::color(0xff, 0xf0, 0x90));
            ui::border(frame.canvas, rect, ui::color(0x80, 0x60, 0x20));
            text(frame, "+1", (rect.left + rect.right) / 2 + (down ? 1 : 0), (rect.top + rect.bottom) / 2 + (down ? 1 : 0),
                 2, topic_style);
            // Host is per frame: keep copies of its callbacks, not a reference.
            hit(host, ids::help_give, rect, no_sound, [sound = host.sound, toast = host.toast, give, cards, id, name] {
                const bool given = give(cards, id);
                if (sound)
                    sound(given ? click_sound : error_sound);
                if (toast)
                    toast(given ? std::string("獲得 ") + name : std::string(cards ? "卡片已滿" : "最多 9 個"));
            });
        }
    }

    // Hit areas, first match wins: the ref_00476254 columns (category x 22-91, topics x 104-196), in
    // finger-sized versions on touch builds.
    for (unsigned i = 0; i < 8; ++i)
        hit(host, ids::help_category + int(i),
            big ? at(14, 56 + 36 * int(i), 98, 92 + 36 * int(i)) : at(22, 56 + 36 * int(i), 91, 92 + 36 * int(i)),
            click_sound,
            [this, i, &artwork = frame.artwork] { select(artwork, i, 0); });
    if (count > visible_topics) {
        hit(host, ids::help_list_up, up_hit, click_sound, [this] { scroll(-1); }, first > 0);
        hit(host, ids::help_list_down, down_hit, click_sound, [this] { scroll(1); }, first + visible_topics < count);
    }
    for (unsigned row = 0; row < visible_topics && first + row < count; ++row)
        hit(host, ids::help_topic + int(row),
            big ? at(100, 77 + 34 * int(row), 204, 111 + 34 * int(row)) : at(104, 78 + 34 * int(row), 196, 112 + 34 * int(row)),
            click_sound,
            [this, index = first + row, &artwork = frame.artwork] { select(artwork, category_, index); });
    if (paged) {
        hit(host, ids::help_page_prev, prev_hit, click_sound, [this] { page_ = page_ ? page_ - 1 : 0; }, page_ > 0);
        hit(host, ids::help_page_next, next_hit, click_sound, [this] { ++page_; }, page_ + 1 < pages_.size());
        // Touch: a tap on the text turns to the next page (wrapping), so reading needs no small target.
        if (big)
            hit(host, ids::help_text, at(210, 86, 380, 380), click_sound,
                [this] { page_ = page_ + 1 < pages_.size() ? page_ + 1 : 0; });
    }
    if (big) { // touch: a tap outside closes it (mouse builds: right-click, through cancel())
        hit(host, ids::help_window, at(0, 0, window_size, window_size), no_sound, [] {});
        hit(host, ids::help_outside, {0, 0, 640, 480}, cancel_sound, [this] { close(); });
    }
}

} // namespace rich4::native::screens
