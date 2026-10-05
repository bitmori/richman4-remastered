/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original SPR and terrain rendering with synthetic images only.
 */
#ifndef RICH4_ORIGINAL_GRAPHICS_H
#define RICH4_ORIGINAL_GRAPHICS_H

static void capture_spr(unsigned char *game) {
    typedef int (__cdecl *spr_fn)(uint16_t *, void *, int, int, int);
    spr_fn render[2] = {(spr_fn)(game + 0x56512), (spr_fn)(game + 0x5663e)};
    /* This is the original, relocated, 32-bit in-memory representation. */
    struct {
        char signature[4];
        uint32_t count, start;
        original_graph frame;
        uint16_t palette[256];
        unsigned char indices[20];
    } sprite = {{'S', 'P', 'R', 0}, 1, 24, {5, 4, 0, 0, NULL}, {0}, {0}};
    sprite.frame.pixels = (uint16_t *)sprite.indices;
    sprite.palette[0] = 0x1234;
    sprite.palette[1] = 0;
    sprite.palette[2] = 0x4567;
    sprite.palette[3] = 0x4321;
    for (int i = 0; i < 20; ++i) sprite.indices[i] = (unsigned char)(i % 4);
    if (sizeof(original_graph) != 12 || (unsigned char *)sprite.palette - (unsigned char *)&sprite != 24)
        fail("unexpected original SPR struct layout");
    uint16_t *destination = malloc((640 * 480 + 16) * sizeof(uint16_t));
    if (!destination) fail("allocate SPR destination");
    FILE *output = fopen("golden\\spr.jsonl", "wb");
    if (!output) fail("open SPR vectors");
    const int anchors[3][2] = {{0, 0}, {-2, 1}, {2, -1}};
    const int placements[][2] = {
        {-6, 1}, {-4, 1}, {-1, 1}, {0, 0}, {1, 1}, {638, 1}, {639, 1}, {640, 1},
        {1, -5}, {1, -3}, {1, -1}, {1, 478}, {1, 479}, {1, 480},
        {-1, -1}, {639, 479}, {638, -2}, {-2, 478}, {100, 100}
    };
    unsigned long count = 0;
    for (int anchor = 0; anchor < 3; ++anchor) {
        sprite.frame.x = (int16_t)anchors[anchor][0];
        sprite.frame.y = (int16_t)anchors[anchor][1];
        for (int mode = 0; mode < 2; ++mode) {
            for (size_t placement = 0; placement < sizeof(placements) / sizeof(placements[0]); ++placement) {
                for (int i = 0; i < 640 * 480 + 16; ++i) destination[i] = 0x7bde;
                int x = placements[placement][0], y = placements[placement][1];
                int result = render[mode](destination, &sprite, 0, x, y);
                for (int i = 640 * 480; i < 640 * 480 + 16; ++i)
                    if (destination[i] != 0x7bde) fail("original SPR wrote past destination");
                fprintf(output, "{\"x\":%d,\"y\":%d,\"anchor_x\":%d,\"anchor_y\":%d,\"transparent\":%d,\"return\":%d,\"changed\":[",
                        x, y, sprite.frame.x, sprite.frame.y, mode, result);
                int first = 1;
                for (int i = 0; i < 640 * 480; ++i) {
                    if (destination[i] != 0x7bde) {
                        fprintf(output, "%s%d,%u", first ? "" : ",", i, destination[i]);
                        first = 0;
                    }
                }
                fputs("]}\n", output);
                ++count;
            }
        }
    }
    if (ferror(output) || fclose(output)) fail("write SPR vectors");
    free(destination);
    printf("captured %lu SPR vectors\n", count);
}

static void capture_terrain(unsigned char *game) {
    typedef void (__cdecl *terrain_fn)(uint16_t *, const unsigned char *, const int16_t *);
    terrain_fn render = (terrain_fn)(game + 0x557a1);
    uint16_t *palette = (uint16_t *)(game + 0x8b6b4);
    uint16_t saved_palette[256];
    memcpy(saved_palette, palette, sizeof(saved_palette));
    for (int i = 0; i < 256; ++i) palette[i] = (uint16_t)((i * 0x123 + 0x11) & 0x7fff);
    unsigned char texture[32 * 32];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) texture[y * 32 + x] = (unsigned char)((x + y * 7) & 255);
    const int16_t corners[][8] = {
        {10,10, 10,42, 42,42, 42,10},
        {10,10, 10,41, 41,41, 41,10},
        {40,40, 40,56, 56,56, 56,40},
        {100,100, 100,164, 164,164, 164,100},
        {10,40, 26,72, 42,40, 26,8},
        {100,300, 124,348, 148,300, 124,252},
        {-8,-8, -8,24, 24,24, 24,-8},
        {470,630, 470,662, 502,662, 502,630},
        {100,-16, 100,16, 132,16, 132,-16},
        {-16,100, -16,132, 16,132, 16,100},
        {10,-80, 10,-48, 42,-48, 42,-80},
        {500,100, 500,132, 532,132, 532,100},
        {120,230, 131,266, 162,257, 147,218},
        {0,0, 0,639, 479,639, 479,0},
        {10,10, 42,10, 42,42, 10,42},
        {100,200, 100,224, 148,240, 148,184},
        {50,50, 50,52, 200,52, 200,50},
        {200,630, 200,662, 232,662, 232,630}
    };
    uint16_t *destination = malloc(640 * 480 * sizeof(uint16_t));
    if (!destination) fail("allocate terrain destination");
    FILE *metadata = fopen("golden\\terrain.jsonl", "wb");
    if (!metadata) fail("open terrain vectors");
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        for (int pixel = 0; pixel < 640 * 480; ++pixel) destination[pixel] = 0x7bde;
        int32_t *clip = (int32_t *)(game + 0x861b8);
        clip[0] = 0; clip[1] = 0; clip[2] = 640; clip[3] = 480;
        if (i == 12) { clip[0] = 235; clip[1] = 128; clip[2] = 260; clip[3] = 152; }
        int32_t *left = (int32_t *)(game + 0x861f0);
        int32_t *right = (int32_t *)(game + 0x86970);
        for (int line = 0; line < 480; ++line) { left[line] = 0x7fff0000; right[line] = (int32_t)0x80000000U; }
        memset(game + 0x870f0, 0, 4 * 480 * sizeof(int32_t));
        render(destination, texture, corners[i]);
        char name[64];
        snprintf(name, sizeof(name), "golden\\terrain-%06lu.bin", (unsigned long)i);
        FILE *output = fopen(name, "wb");
        if (!output || fwrite(destination, sizeof(uint16_t), 640 * 480, output) != 640 * 480 || fclose(output))
            fail("write terrain pixels");
        fprintf(metadata, "{\"file\":\"terrain-%06lu.bin\",\"corners\":[", (unsigned long)i);
        for (int j = 0; j < 8; ++j) fprintf(metadata, "%s%d", j ? "," : "", corners[i][j]);
        fprintf(metadata, "],\"clip\":[%ld,%ld,%ld,%ld]}\n", (long)clip[0], (long)clip[1], (long)clip[2], (long)clip[3]);
    }
    if (ferror(metadata) || fclose(metadata)) fail("write terrain metadata");
    free(destination);
    memcpy(palette, saved_palette, sizeof(saved_palette));
    puts("captured 18 terrain vectors");
}
#endif
