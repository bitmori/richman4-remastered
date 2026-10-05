/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure original-machine-code calendar and direction vectors.
 */
#ifndef RICH4_ORIGINAL_LOGIC_H
#define RICH4_ORIGINAL_LOGIC_H

static void capture_calendar(unsigned char *game) {
    typedef int (__cdecl *days_fn)(uint32_t);
    typedef uint32_t (__cdecl *date_fn)(int);
    typedef void (__cdecl *parse_fn)(uint32_t, int *, int *);
    typedef int (__cdecl *advance_fn)(uint32_t *);
    typedef uint32_t (__cdecl *add_fn)(uint32_t, int);
    typedef int (__cdecl *difference_fn)(uint32_t, uint32_t);
    typedef uint32_t (__cdecl *packed_fn)(uint32_t, uint32_t);
    days_fn days = (days_fn)(game + 0x51f8c);
    date_fn date = (date_fn)(game + 0x5201f);
    parse_fn parse = (parse_fn)(game + 0x520a6);
    advance_fn advance = (advance_fn)(game + 0x52117);
    add_fn add = (add_fn)(game + 0x5218f);
    difference_fn difference = (difference_fn)(game + 0x521aa);
    packed_fn packed = (packed_fn)(game + 0x521cb);
    FILE *output = fopen("golden\\calendar.csv", "wb");
    if (!output) fail("open calendar vectors");
    fputs("days,date,weekday,month_days,next_date,month_changed,plus_400,minus_400,difference\n", output);
    int limit = days((2201U << 16) | (1U << 8) | 1U);
    for (int day = 0; day < limit; ++day) {
        uint32_t value = date(day), next = value;
        int weekday = -1, month_days = -1;
        parse(value, &weekday, &month_days);
        if (days(value) != day) fail("original calendar roundtrip self-check");
        int month_changed = advance(&next);
        uint32_t plus = add(value, 400);
        uint32_t minus = day >= 400 ? add(value, -400) : 0;
        fprintf(output, "%d,%lu,%d,%d,%lu,%d,%lu,%lu,%d\n", day, (unsigned long)value,
                weekday, month_days, (unsigned long)next, month_changed,
                (unsigned long)plus, (unsigned long)minus, difference(value, plus));
    }
    if (ferror(output) || fclose(output)) fail("write calendar vectors");
    output = fopen("golden\\calendar_packed.csv", "wb");
    if (!output) fail("open packed calendar vectors");
    fputs("date,delta,result\n", output);
    const uint32_t inputs[] = {
        0, 1, 0xffffffffU, (1998U << 16) | 0x0101, (2000U << 16) | 0x021d,
        (2100U << 16) | 0x021d, (2200U << 16) | 0x0c1f, (2024U << 16) | 0x061e
    };
    const uint32_t deltas[] = {1, 0x100, 0x200, 0x600, 0xc00, 0x10000, 0xffffff00U, 0xffffffffU};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        for (size_t j = 0; j < sizeof(deltas) / sizeof(deltas[0]); ++j)
            fprintf(output, "%lu,%lu,%lu\n", (unsigned long)inputs[i], (unsigned long)deltas[j],
                    (unsigned long)packed(inputs[i], deltas[j]));
    if (ferror(output) || fclose(output)) fail("write packed calendar vectors");
    printf("captured %d calendar days and 64 packed increments\n", limit);
}

static void capture_direction(unsigned char *game) {
    typedef int (__cdecl *direction_fn)(int32_t, int32_t);
    direction_fn direction = (direction_fn)(game + 0x54fb4);
    FILE *output = fopen("golden\\direction.csv", "wb");
    if (!output) fail("open direction vectors");
    fputs("dx,dy,direction\n", output);
    unsigned long count = 0;
    for (int32_t y = -128; y <= 128; ++y) {
        for (int32_t x = -128; x <= 128; ++x) {
            fprintf(output, "%ld,%ld,%d\n", (long)x, (long)y, direction(x, y));
            ++count;
        }
    }
    for (int32_t value = 0; value <= 2048; ++value) {
        const int32_t pairs[8][2] = {
            {2048, value}, {value, 2048}, {-2048, value}, {-value, 2048},
            {2048, -value}, {value, -2048}, {-2048, -value}, {-value, -2048}
        };
        for (size_t i = 0; i < 8; ++i) {
            fprintf(output, "%ld,%ld,%d\n", (long)pairs[i][0], (long)pairs[i][1], direction(pairs[i][0], pairs[i][1]));
            ++count;
        }
    }
    const int32_t edges[] = {INT32_MIN, INT32_MIN + 1, -65536, -32768, -1, 0, 1, 32767, 65535, INT32_MAX - 1, INT32_MAX};
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        for (size_t j = 0; j < sizeof(edges) / sizeof(edges[0]); ++j) {
            fprintf(output, "%ld,%ld,%d\n", (long)edges[i], (long)edges[j], direction(edges[i], edges[j]));
            ++count;
        }
    }
    if (ferror(output) || fclose(output)) fail("write direction vectors");
    printf("captured %lu direction vectors\n", count);
}
#endif
