// SPDX-License-Identifier: GPL-3.0-or-later
// Clerk / announcer voices of the original screens. Every Speaking.mkf clip is triggered by a
// "#NNNN" prefix on the string the screen draws (_rich4_draw_text, rich4_text_drawing.asm:349);
// the strings below are those of rich4.exe (0x46423c.., 0x46484c.., 0x464ed7.., 0x465170..),
// so a native screen showing one of these lines plays the same clip. The original's "∼" is
// written "～" here (both spellings match).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rich4::native::voices {

struct ClerkLine {
    std::uint16_t voice;
    std::string_view text;
};

inline constexpr ClerkLine clerk_lines[] = {
    // Card shop / tool shop (rich4_shop.asm).
    {0, "有什麼我能\n為你服務的嗎？"}, {1, "請挑選你想要\n兌換的卡片。"}, {2, "抱歉！\n你的點數不足！"},
    {3, "對不起！\n您的卡片欄已滿！"}, {4, "歡迎下次再來！"}, {5, "歡迎光臨\n道具店！"},
    {6, "您要兌換\n什麼道具？"}, {7, "對不起，\n您的點券不夠！"}, {8, "很抱歉！您的\n道具欄已滿！"},
    {9, "這個道具會員\n才能兌換！"}, {10, "謝謝惠顧！"},
    // Lottery kiosk (_rich4_ui_letou_bar_entry).
    {11, "哈囉！\n一券在手，\n希望無窮！"}, {12, "只要一千元，\n就有獲得大獎\n的機會！"},
    {13, "請圈選您的\n幸運號碼～"}, {14, "拜拜！祝您中獎！"}, {15, "太可惜了！\n您的現金不足～"},
    {16, "下次再來吧！"},
    // Magic house witch (rich4_magic_house.asm fcn_004325c2) and god-of-death prompts.
    {37, "進來魔法屋，就得\n完全照我的指示！"}, {38, "我選出符合條件的人。"}, {39, "你來決定他們的命運～"},
    {40, "嘿～輪到你了！"}, {41, "天靈靈地靈靈～"}, {42, "你想召喚死神，\n為你復仇嗎？"},
    {43, "你要死神附身在誰身上？"}, {45, "嘿！你真仁慈～"},
    // Bank clerk and director (rich4_ui_bank.asm).
    {75, "歡迎光臨\n大富翁銀行！"}, {76, "需要我為您\n服務嗎？"}, {77, "您的貸款額度\n為一百萬元。"},
    {78, "請輸入您要貸款\n的金額！"}, {79, "您的貸款手續\n已經完成。"}, {80, "請於三個月內\n還清貸款。"},
    {81, "很抱歉！\n金額已超過\n許可額度。"}, {82, "請輸入您預備\n還款的金額！"},
    {83, "很抱歉！\n您的現金不足。"}, {84, "您的還款手續\n已完成。"}, {85, "謝謝您的惠顧！"},
    {86, "董事長親自蒞臨\n不知有何指教？"}, {87, "請輸入您要\n週轉的金額～"},
    {88, "行裡現在沒有\n這麼多現金。"}, {89, "請輸入您要\n還款的金額～"},
    {89, "請輸入您預備\n還款的金額～"}, // native director wording of 89
    {90, "董事長您別開玩笑了～"}, {91, "董事長慢走！"},
    // Rescued NPC thanks (_special_player_strings, slots 4..7 = 124, 125, 123, 126).
    {123, "太感激了！我一\n定會報答你的！"}, {124, "謝謝你！你真是\n我的再生父母！"},
    {125, "我先走了！大恩\n大德來日再報！"}, {126, "受人點水之恩，\n必當湧泉已報！"},
    // Hospital nurse (rich4_ui_hospital.asm).
    {127, "您好！請問您要替誰\n辦理出院手續？"}, {128, "請稍後！"}, {129, "ＯＫ！您的朋友已經\n可以出院了！"},
    {130, "要保重身體喔！"},
    // Auction announcer (rich4_ui_auction.asm); 132/135 carry %d and 136+c a buyer name.
    {131, "公開拍賣土地一處。"}, {148, "無人出價，宣佈流標。"},
    // Magic-house target groups 46..57 (spoken after 38/39).
    {46, "財產最多的人"}, {47, "土地最多的人"}, {48, "房屋最多的人"}, {49, "現金最多的人"},
    {50, "存款最多的人"}, {51, "點券最多的人"}, {52, "走路的人"}, {53, "騎機車的人"},
    {54, "開汽車的人"}, {55, "神明附身的人"}, {56, "所有男生"}, {57, "所有女生"},
};

// Speaking.mkf clip for a clerk line as drawn by a native screen, if the original voices it.
inline std::optional<std::uint16_t> clerk_voice(std::string_view text) {
    std::string normal(text);
    for (std::size_t at; (at = normal.find("∼")) != std::string::npos;)
        normal.replace(at, std::string_view("∼").size(), "～");
    for (const auto &line : clerk_lines)
        if (line.text == normal)
            return line.voice;
    return std::nullopt;
}

// Magic-house target group 0..11 (財產最多的人 .. 所有女生) -> clip 46..57.
inline constexpr std::uint16_t magic_group_voice(int group) {
    return static_cast<std::uint16_t>(46 + group);
}
// Prison/hospital rescue of special actor slot 4..7 -> its thanks line.
inline constexpr std::uint16_t rescued_npc_voice(int slot) {
    constexpr std::array<std::uint16_t, 4> thanks{124, 125, 123, 126};
    return thanks[std::size_t(slot - 4) & 3U];
}
// Auction result: "恭喜<buyer>購得此地！" 136 + character, or 148 for no sale.
inline constexpr std::uint16_t auction_buyer_voice(int character) {
    return static_cast<std::uint16_t>(136 + character % 12);
}

// Plays each new line once: remembers the last text shown so a line held over many frames is
// voiced only when it first appears (the original voices a string when it is drawn).
class LineVoice {
  public:
    template <class Speak> void show(std::string_view text, const Speak &speak) {
        if (text == last_)
            return;
        last_ = std::string(text);
        if (const auto voice = clerk_voice(text))
            speak(std::size_t(*voice));
    }
    void reset() { last_.clear(); }

  private:
    std::string last_;
};

} // namespace rich4::native::voices
