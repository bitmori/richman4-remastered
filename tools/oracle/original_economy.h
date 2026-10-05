/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original-machine-code adapter against synthetic land data; no game assets.
 */
#ifndef RICH4_ORIGINAL_ECONOMY_H
#define RICH4_ORIGINAL_ECONOMY_H

static void economy_set_u16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static const unsigned char economy_names[6][16] = {
    {0}, {0xa4, 0x40, 0}, {0xa4, 0x41, 0}, {'A', 'B', 0},
    {0xa4, 0x40, 0xa4, 0x41, 0}, {'a', 'b', 0}
};

static void economy_fill_land(unsigned char *record, unsigned index, unsigned variant) {
    memset(record, 0xa9, 52);
    memcpy(record + 4, economy_names[(index + variant) % 6], 16);
    static const unsigned char chain_flags[4] = {0, 0, 1, 255};
    static const unsigned char owners[6] = {0, 1, 2, 3, 4, 255};
    record[0x18] = chain_flags[(index + variant) % 4];
    record[0x19] = owners[(index + 2 * variant) % 6];
    record[0x1a] = (unsigned char)((index + variant) % 6);
    for (unsigned level = 0; level < 6; ++level)
        economy_set_u16(record + 0x20 + level * 2, variant == 2 ? 65535 :
                        (index * 211 + level * 73 + variant * 40000) % 65536);
}

static void economy_emit_case(FILE *output, unsigned char *game, unsigned char *records,
                              unsigned count, int owner, int32_t price, int street_kind,
                              const unsigned char street[16], int repeated_record) {
    typedef int (__cdecl *count_fn)(int);
    typedef int (__cdecl *toll_fn)(int, const char *);
    /* Extra NUL padding is needed by original strcmp's 32-bit lookahead. */
    unsigned char query[32] = {0};
    memcpy(query, street, 16);
    *(unsigned char **)(game + 0x98e84) = records;
    *(int32_t *)(game + 0x98e98) = (int32_t)count;
    *(int32_t *)(game + 0x990e8) = price;
    int chains = ((count_fn)(game + 0x1970f))(owner);
    int toll = ((toll_fn)(game + 0x19744))(owner, street_kind ? (const char *)query : NULL);
    fprintf(output, "%d %ld %d", owner, (long)price, street_kind);
    for (size_t i = 0; i < 16; ++i) fprintf(output, " %u", street[i]);
    const unsigned stored = repeated_record ? 1 : count;
    fprintf(output, " %u %u", count, stored);
    for (unsigned i = 0; i < stored; ++i) {
        const unsigned char *record = records + (i + 1) * 52;
        for (size_t byte = 0; byte < 52; ++byte) fprintf(output, " %u", record[byte]);
    }
    fprintf(output, " %d %d\n", chains, toll);
}

static void capture_economy(unsigned char *game) {
    unsigned char *saved_pointer = *(unsigned char **)(game + 0x98e84);
    int32_t saved_count = *(int32_t *)(game + 0x98e98);
    int32_t saved_price = *(int32_t *)(game + 0x990e8);
    FILE *output = fopen("golden\\economy.txt", "wb");
    if (!output) fail("open economy vectors");
    fputs("rich4-economy-v1\n0000000000\n", output);
    const unsigned counts[] = {0, 1, 6, 24};
    const int owners[] = {-1, 0, 1, 2, 3, 4, 255, 256};
    const int32_t prices[] = {0, 1, 2, -1, INT32_MAX, INT32_MIN, 65537};
    unsigned long cases = 0;
    unsigned char records[25 * 52];
    for (unsigned variant = 0; variant < 3; ++variant) {
        /* Sentinel deliberately matches an owner and has nonzero rent: original
         * loops must skip it, including when num_lands is zero. */
        for (unsigned i = 0; i <= 24; ++i) economy_fill_land(records + i * 52, i, variant);
        records[0x18] = 255;
        records[0x19] = 1;
        for (size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n) {
            for (size_t owner = 0; owner < sizeof(owners) / sizeof(owners[0]); ++owner) {
                for (size_t price = 0; price < sizeof(prices) / sizeof(prices[0]); ++price) {
                    for (int street = -1; street < 6; ++street) {
                        const unsigned char *name = economy_names[street < 0 ? 0 : street];
                        economy_emit_case(output, game, records, counts[n], owners[owner], prices[price],
                                          street >= 0, name, 0);
                        ++cases;
                    }
                }
            }
        }
    }
    /* Overflow the addition itself: 65538 * 65535 wraps to 65534. Store one
     * repeat record in the vector while exercising the actual expanded array. */
    const unsigned large_count = 65538;
    unsigned char *large_records = calloc((size_t)(large_count + 1), 52);
    if (!large_records) fail("allocate economy addition-overflow input");
    economy_fill_land(large_records + 52, 0, 2);
    large_records[52 + 0x18] = 0;
    large_records[52 + 0x19] = 1;
    memcpy(large_records + 52 + 4, economy_names[1], 16);
    for (unsigned i = 2; i <= large_count; ++i) memcpy(large_records + i * 52, large_records + 52, 52);
    economy_emit_case(output, game, large_records, large_count, 1, 1, 1, economy_names[1], 1);
    ++cases;
    free(large_records);
    *(unsigned char **)(game + 0x98e84) = saved_pointer;
    *(int32_t *)(game + 0x98e98) = saved_count;
    *(int32_t *)(game + 0x990e8) = saved_price;
    if (fseek(output, (long)strlen("rich4-economy-v1\n"), SEEK_SET) ||
        fprintf(output, "%010lu", cases) < 0 || ferror(output) || fclose(output))
        fail("write economy vectors");
    printf("captured %lu original economy cases\n", cases);
    fflush(stdout);
}

#endif
