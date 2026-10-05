/* SPDX-License-Identifier: GPL-3.0-or-later
 * Adapter only: executes the user's original Rich4 machine code against
 * synthetic inventories in an isolated process. No original game code/data.
 */
#ifndef RICH4_ORIGINAL_INVENTORY_H
#define RICH4_ORIGINAL_INVENTORY_H

typedef struct {
    FILE *output;
    unsigned char *game;
    unsigned char prices[30], tool_prices[13];
    unsigned long cases;
} inventory_capture;

static void inventory_write_bytes(FILE *output, const unsigned char *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) fprintf(output, " %u", bytes[i]);
}

static void inventory_case(inventory_capture *capture, int operation, int argument,
                           const unsigned char cards[15], const unsigned char supply[30],
                           const unsigned char amounts[15], const unsigned char tool_supply[8]) {
    typedef int (__cdecl *one_arg_fn)(int);
    typedef int (__cdecl *two_arg_fn)(int, int);
    typedef void (__cdecl *mutate_fn)(int, int);
    unsigned char *game = capture->game;
    unsigned char *all_cards = game + 0x99120;
    unsigned char *all_tools = game + 0x9915c;
    const int player = (int)(capture->cases % 4);
    unsigned char other_cards[60], other_tools[60];
    memset(all_cards, 0x1d, 60);
    memset(all_tools, 0xb7, 60);
    memcpy(all_cards + 15 * player, cards, 15);
    memcpy(all_tools + 15 * player, amounts, 15);
    memcpy(game + 0x99198, supply, 30);
    memcpy(game + 0x97320, tool_supply, 8);
    memcpy(other_cards, all_cards, 60);
    memcpy(other_tools, all_tools, 60);
    /* The original sale's non-inventory transport branch calls sprite code.
     * Test only the pure inventory branch with traffic_method == 0. */
    game[0x96b68 + player * 0x68 + 0x11] = 0;
    int result = -1; /* Mutating void routines have no defined return value. */
    switch (operation) {
        case 0: result = ((one_arg_fn)(game + 0x41262))(player); break;
        case 1: result = ((one_arg_fn)(game + 0x4128f))(player); break;
        case 2: ((mutate_fn)(game + 0x412e4))(player, argument); break;
        case 3: ((mutate_fn)(game + 0x41343))(player, argument); break;
        case 4: result = ((two_arg_fn)(game + 0x413ad))(player, argument); break;
        case 5: result = ((one_arg_fn)(game + 0x41ece))(player); break;
        case 6: result = ((one_arg_fn)(game + 0x41f21))(player); break;
        case 7: ((mutate_fn)(game + 0x45a4d))(player, argument); break;
        case 8: ((mutate_fn)(game + 0x45aa2))(player, argument); break;
        case 9: result = ((one_arg_fn)(game + 0x45b3f))(player); break;
        default: fail("unknown inventory operation");
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (slot != player &&
            (memcmp(other_cards + slot * 15, all_cards + slot * 15, 15) ||
             memcmp(other_tools + slot * 15, all_tools + slot * 15, 15)))
            fail("original inventory routine altered another player's inventory");
    }
    fprintf(capture->output, "%d %d %d", operation, argument, result);
    inventory_write_bytes(capture->output, capture->prices, 30);
    inventory_write_bytes(capture->output, capture->tool_prices, 13);
    inventory_write_bytes(capture->output, cards, 15);
    inventory_write_bytes(capture->output, supply, 30);
    inventory_write_bytes(capture->output, amounts, 15);
    inventory_write_bytes(capture->output, tool_supply, 8);
    inventory_write_bytes(capture->output, all_cards + player * 15, 15);
    inventory_write_bytes(capture->output, game + 0x99198, 30);
    inventory_write_bytes(capture->output, all_tools + player * 15, 15);
    inventory_write_bytes(capture->output, game + 0x97320, 8);
    fputc('\n', capture->output);
    ++capture->cases;
}

static void capture_inventory(unsigned char *game) {
    inventory_capture capture = {0};
    capture.game = game;
    unsigned char saved_cards[60], saved_tools[60], saved_supply[30], saved_tool_supply[8];
    unsigned char saved_players[4 * 0x68], original_prices[30], original_tool_prices[13];
    memcpy(saved_cards, game + 0x99120, 60);
    memcpy(saved_tools, game + 0x9915c, 60);
    memcpy(saved_supply, game + 0x99198, 30);
    memcpy(saved_tool_supply, game + 0x97320, 8);
    memcpy(saved_players, game + 0x96b68, sizeof(saved_players));
    for (size_t i = 0; i < 30; ++i) original_prices[i] = game[0x7fdf2 + i * 8 + 5];
    for (size_t i = 0; i < 13; ++i) original_tool_prices[i] = game[0x7fee2 + i * 8 + 5];
    DWORD previous_protection;
    if (!VirtualProtect(game + 0x7fdf2, 0x160, PAGE_READWRITE, &previous_protection))
        fail("make isolated inventory price inputs writable");
    capture.output = fopen("golden\\inventory.txt", "wb");
    if (!capture.output) fail("open inventory vectors");
    fputs("rich4-inventory-v1\n0000000000\n", capture.output);
    unsigned char cards[15], supply[30], amounts[15], tool_supply[8];
    const unsigned char supplies[] = {0, 1, 254, 255};
    for (int price_mode = 0; price_mode < 3; ++price_mode) {
        for (size_t i = 0; i < 30; ++i) {
            capture.prices[i] = price_mode == 0 ? original_prices[i] :
                (price_mode == 1 ? 17 : (unsigned char)(255 - i * 17));
            game[0x7fdf2 + i * 8 + 5] = capture.prices[i];
        }
        for (size_t i = 0; i < 13; ++i) {
            capture.tool_prices[i] = price_mode == 0 ? original_tool_prices[i] :
                (price_mode == 1 ? 255 : (unsigned char)(i * 21));
            game[0x7fee2 + i * 8 + 5] = capture.tool_prices[i];
        }
        for (int count = 0; count <= 15; ++count) {
            for (int pattern = 0; pattern < 6; ++pattern) {
                memset(cards, 0, sizeof(cards));
                for (int i = 0; i < count; ++i) {
                    switch (pattern) {
                        case 0: cards[i] = (unsigned char)(i + 1); break;
                        case 1: cards[i] = 30; break;
                        case 2: cards[i] = (unsigned char)(1 + (i + 1) / 2); break;
                        case 3: cards[i] = (unsigned char)(1 + (i * 7 + count) % 30); break;
                        case 4: cards[i] = (unsigned char)(i % 2 == 0 ? 1 : 30); break;
                        case 5: cards[i] = (unsigned char)(30 - i); break;
                    }
                }
                for (size_t variant = 0; variant < sizeof(supplies); ++variant) {
                    memset(supply, supplies[variant], sizeof(supply));
                    memset(amounts, 3, sizeof(amounts));
                    memset(tool_supply, 13, sizeof(tool_supply));
                    for (int operation = 0; operation <= 6; ++operation) {
                        int argument = 1 + (count + pattern * 7 + (int)variant) % 30;
                        if (operation == 4 && variant == 0) argument = 0;
                        inventory_case(&capture, operation, argument, cards, supply, amounts, tool_supply);
                    }
                }
            }
        }
        /* Sparse rows are supported by all card operations except drop_half:
         * that routine can consume ID 0 and write before the supply array. */
        for (int id = 1; id <= 30; ++id) {
            memset(cards, 0, sizeof(cards));
            cards[0] = (unsigned char)id;
            cards[7] = (unsigned char)(31 - id);
            cards[14] = (unsigned char)id;
            memset(supply, 255, sizeof(supply));
            for (int operation = 0; operation <= 6; ++operation) {
                if (operation != 5)
                    inventory_case(&capture, operation, id, cards, supply, amounts, tool_supply);
            }
        }
        const unsigned char tool_amounts[] = {0, 1, 8, 9, 254, 255};
        memset(cards, 0, sizeof(cards));
        memset(supply, 21, sizeof(supply));
        for (int tool = 1; tool <= 13; ++tool) {
            for (size_t amount = 0; amount < sizeof(tool_amounts); ++amount) {
                for (size_t remaining = 0; remaining < sizeof(supplies); ++remaining) {
                    memset(amounts, tool_amounts[amount], sizeof(amounts));
                    memset(tool_supply, supplies[remaining], sizeof(tool_supply));
                    for (int operation = 7; operation <= 9; ++operation)
                        inventory_case(&capture, operation, tool, cards, supply, amounts, tool_supply);
                }
            }
        }
    }
    if (fseek(capture.output, (long)strlen("rich4-inventory-v1\n"), SEEK_SET) ||
        fprintf(capture.output, "%010lu", capture.cases) < 0 || ferror(capture.output) || fclose(capture.output))
        fail("write inventory vectors");
    for (size_t i = 0; i < 30; ++i) game[0x7fdf2 + i * 8 + 5] = original_prices[i];
    for (size_t i = 0; i < 13; ++i) game[0x7fee2 + i * 8 + 5] = original_tool_prices[i];
    DWORD unused;
    if (!VirtualProtect(game + 0x7fdf2, 0x160, previous_protection, &unused))
        fail("restore isolated price memory protection");
    memcpy(game + 0x99120, saved_cards, 60);
    memcpy(game + 0x9915c, saved_tools, 60);
    memcpy(game + 0x99198, saved_supply, 30);
    memcpy(game + 0x97320, saved_tool_supply, 8);
    memcpy(game + 0x96b68, saved_players, sizeof(saved_players));
    printf("captured %lu original inventory cases\n", capture.cases);
    fflush(stdout);
}

#endif
