/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic test adapter, not a replacement stock implementation. Calls the
 * verified user's EXE in a private process. No commercial code/data is copied
 * into this source. The sole patched function refreshes minimap pixels; stock
 * arithmetic, ranking and flags execute the original instructions.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t (__cdecl *price_fn)(uint32_t, uint32_t);
typedef void (__cdecl *update_fn)(int);
typedef void (__cdecl *sell_fn)(int, int, int, int);

static void fail(const char* reason) {
    FILE* f = fopen("failure.txt", "wb");
    if (f) { fputs(reason, f); fclose(f); }
    ExitProcess(2);
}

__attribute__((noinline)) static uint32_t price(void* address, uint32_t before, uint32_t percent) {
    price_fn volatile target = (price_fn)address;
    return target(before, percent);
}

static uint32_t bits(float value) { uint32_t result; memcpy(&result, &value, 4); return result; }
static void word(void* p, uint16_t value) { memcpy(p, &value, 2); }
static void dword(void* p, uint32_t value) { memcpy(p, &value, 4); }
static uint32_t read32(const void* p) { uint32_t value; memcpy(&value, p, 4); return value; }
static uint16_t read16(const void* p) { uint16_t value; memcpy(&value, p, 2); return value; }

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    (void)instance; (void)previous; (void)command; (void)show;
    unsigned char* game = (unsigned char*)LoadLibraryExA("rich4.exe", NULL, DONT_RESOLVE_DLL_REFERENCES);
    if ((uintptr_t)game != 0x400000) fail("original EXE did not load at 0x400000");
    if (memcmp(game + 0x28ec5, "\x83\xec\x10", 3)) fail("wrong price helper entry");
    const uint16_t control = 0x037f; /* nearest, 64-bit x87 significand */
    __asm__ volatile("fldcw %0" : : "m"(control));
    if (price(game + 0x28ec5, bits(1000.0f), bits(10.0f)) != bits(1100.0f))
        fail("original price call self-check failed");
    FILE* prices = fopen("prices.csv", "wb");
    if (!prices) fail("open prices.csv");
    fputs("price_bits,percent_bits,result_bits\n", prices);
    const float rates[] = {-10, -5, -1, 0, 1, 5, 10};
    for (unsigned i = 100; i <= 100000; i += 7) {
        float before = i / 100.0f;
        for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
            fprintf(prices, "%lu,%lu,%lu\n", (unsigned long)bits(before), (unsigned long)bits(rates[r]),
                    (unsigned long)price(game + 0x28ec5, bits(before), bits(rates[r])));
    }
    const float boundaries[] = {1, 5, 15, 50, 150, 9999};
    for (unsigned b = 0; b < sizeof(boundaries) / sizeof(boundaries[0]); ++b) {
        for (int delta = -8; delta <= 8; ++delta) {
            const uint32_t before = bits(boundaries[b]) + delta;
            for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
                fprintf(prices, "%lu,%lu,%lu\n", (unsigned long)before, (unsigned long)bits(rates[r]),
                        (unsigned long)price(game + 0x28ec5, before, bits(rates[r])));
        }
    }
    if (ferror(prices) || fclose(prices)) fail("write prices.csv");

    /* Suppress only the minimap redraw that follows an ownership change. */
    DWORD protection;
    if (!VirtualProtect(game + 0xa4e1, 1, PAGE_EXECUTE_READWRITE, &protection)) fail("protect refresh stub");
    game[0xa4e1] = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), game + 0xa4e1, 1);
    unsigned char commercial[2 * 52];
    unsigned char* stocks = game + 0x96980;
    unsigned char* holdings = game + 0x971a0;
    unsigned char* players = game + 0x96b68;
    dword(game + 0x98e7c, (uint32_t)(uintptr_t)commercial);
    FILE* sales = fopen("sales.csv", "wb");
    if (!sales) fail("open sales.csv");
    fputs("player,amount,credit_bank,price_bits,hold0,hold1,hold2,hold3,rank_before,owner_before,proceeds,bank_after,fund_after,remaining,basis_after,available,quota,rank_after,owner_after\n", sales);
    for (unsigned player_id = 0; player_id < 4; ++player_id) {
        for (unsigned amount = 0; amount <= 600; amount += 50) {
            for (unsigned bank = 0; bank < 2; ++bank) {
                memset(commercial, 0, sizeof(commercial));
                memset(stocks, 0, 12 * 36); memset(holdings, 0, 4 * 12 * 8); memset(players, 0, 4 * 104);
                const unsigned amounts[4] = {1000, 900, 800, 700};
                for (unsigned p = 0; p < 4; ++p) {
                    dword(holdings + p * 96, amounts[p]); dword(holdings + p * 96 + 4, bits(21.25f));
                    dword(players + p * 104 + 32, 10000);
                }
                word(stocks + 4, 1); word(stocks + 8, 6600); word(stocks + 10, 1500);
                const float quote = 15.55f + player_id;
                dword(stocks + 20, bits(quote)); dword(game + 0x99080, 2000);
                commercial[52 + 24] = 1; dword(commercial + 52 + 28, 0x04030201);
                sell_fn volatile sell = (sell_fn)(game + 0x28e23);
                sell((int)player_id, 0, (int)amount, (int)bank);
                const uint32_t bank_after = read32(players + player_id * 104 + 32);
                const uint32_t fund_after = read32(game + 0x99080);
                fprintf(sales, "%u,%u,%u,%lu,1000,900,800,700,67305985,1,%lu,%lu,%lu,%lu,%lu,%u,%u,%lu,%u\n",
                        player_id, amount, bank, (unsigned long)bits(quote),
                        (unsigned long)(bank ? bank_after - 10000 : fund_after - 2000),
                        (unsigned long)bank_after, (unsigned long)fund_after,
                        (unsigned long)read32(holdings + player_id * 96), (unsigned long)read32(holdings + player_id * 96 + 4),
                        read16(stocks + 8), read16(stocks + 10),
                        (unsigned long)read32(commercial + 52 + 28), commercial[52 + 24]);
            }
        }
    }
    if (ferror(sales) || fclose(sales)) fail("write sales.csv");
    FILE* updates = fopen("updates.csv", "wb");
    if (!updates) fail("open updates.csv");
    fputs("cursor,selection,index,flag,previous_bits,current_bits,change_bits,history_bits\n", updates);
    for (unsigned cursor = 0; cursor < 144; cursor += 11) {
        for (unsigned selection = 0; selection <= 12; ++selection) {
            memset(stocks, 0, 12 * 36); memset(game + 0x97328, 0, 12 * 144 * 4);
            dword(game + 0x99100, cursor);
            for (unsigned i = 0; i < 12; ++i) {
                stocks[i * 36 + 7] = i % 3 == 0 ? 0 : i % 3 == 1 ? 2 : 0x20;
                dword(stocks + i * 36 + 16, bits(2.55f + 23.05f * i));
                dword(stocks + i * 36 + 20, bits(123.0f));
            }
            update_fn volatile update = (update_fn)(game + 0x29040);
            update((int)selection);
            if (read32(game + 0x99100) != cursor) fail("forced stock update advanced history");
            const unsigned history_index = cursor == 0 ? 143 : cursor - 1;
            for (unsigned i = 0; i < 12; ++i)
                fprintf(updates, "%u,%u,%u,%u,%lu,%lu,%lu,%lu\n", cursor, selection, i, stocks[i * 36 + 7],
                        (unsigned long)read32(stocks + i * 36 + 16), (unsigned long)read32(stocks + i * 36 + 20),
                        (unsigned long)read32(stocks + i * 36 + 28),
                        (unsigned long)read32(game + 0x97328 + (i * 144 + history_index) * 4));
        }
    }
    if (ferror(updates) || fclose(updates)) fail("write updates.csv");
    FILE* completion = fopen("complete.txt", "wb");
    if (!completion || fputs("original-stocks-v1-self-checked\n", completion) == EOF || fclose(completion)) fail("write completion");
    ExitProcess(0);
}
