/* SPDX-License-Identifier: GPL-3.0-or-later
 * Test adapter only: calls the user's unmodified Rich4 v3.11 machine code.
 * This source contains no game code or assets. Build as a 32-bit PE at an
 * image base other than 0x400000 (see scripts/run_asset_oracle.py).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (__cdecl *decompress_fn)(void *destination, const void *source);
enum { GUARD_SIZE = 4096, MAX_ENTRY_SIZE = 64 * 1024 * 1024 };

static void fail(const char *message) {
    fprintf(stderr, "oracle: %s (Win32 %lu)\n", message, GetLastError());
    exit(2);
}

#include "original_inventory.h"
#include "original_economy.h"
#include "original_logic.h"
#include "original_random.h"

static void decoder_self_check(decompress_fn decode) {
    /* Independently encoded literals A, B, C then distance=0xfff EOS.
     * Four trailing bytes support the original routine's 32-bit lookahead. */
    static const unsigned char encoded[] = {0xfb, 0x0f, 0x1c, 0xf4, 0x03, 0xf8, 0x01, 0, 0, 0, 0};
    unsigned char output[4096];
    memset(output, 0xa5, sizeof(output));
    decode(output, encoded);
    if (memcmp(output, "ABC", 3)) fail("synthetic ABC original decoder self-check failed");
    for (size_t i = 3; i < sizeof(output); ++i)
        if (output[i] != 0xa5) fail("synthetic ABC original decoder exceeded EOS");
}

static uint32_t read_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    if (!stream || fseek(stream, 0, SEEK_END)) fail("open input");
    long size = ftell(stream);
    if (size < 0 || fseek(stream, 0, SEEK_SET)) fail("measure input");
    unsigned char *bytes = malloc((size_t)size + 4);
    if (!bytes || fread(bytes, 1, size, stream) != (size_t)size) fail("read input");
    fclose(stream);
    memset(bytes + size, 0, 4); /* Original routine performs 32-bit lookahead. */
    *length = (size_t)size;
    return bytes;
}

static void make_directory(const char *path) {
    if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        fail("create output directory");
}

static void capture_pixels(unsigned char *game) {
    typedef void (__cdecl *convert_fn)(uint16_t *, int);
    convert_fn convert = (convert_fn)(game + 0x51801);
    uint32_t *format = (uint32_t *)(game + 0x7637c);
    uint16_t *pixels = malloc(65536 * sizeof(*pixels));
    if (!pixels) fail("allocate pixel vectors");
    for (uint32_t mode = 0; mode <= 3; ++mode) {
        *format = mode;
        for (uint32_t i = 0; i < 65536; ++i) pixels[i] = (uint16_t)i;
        convert(pixels, 65536 * 2);
        for (uint32_t i = 0; i < 65536; ++i) {
            uint16_t one[2] = {(uint16_t)i, 0xa55a};
            convert(one, 2);
            if (one[0] != pixels[i] || one[1] != 0xa55a)
                fail("single/bulk original pixel conversion disagree");
        }
        char path[64];
        snprintf(path, sizeof(path), "golden\\pixels-%lu.bin", (unsigned long)mode);
        FILE *output = fopen(path, "wb");
        if (!output || fwrite(pixels, sizeof(*pixels), 65536, output) != 65536 || fclose(output))
            fail("write pixel vectors");
    }
    *format = 0;
    free(pixels);
    puts("captured all 65536 pixel inputs for formats 0..3 (single and paired paths)");
}

typedef struct {
    int16_t width, height, x, y;
    uint16_t *pixels;
} original_graph;

static void capture_blits(unsigned char *game) {
    typedef int (__cdecl *opaque_fn)(int, int, uint16_t *, const original_graph *, int, int);
    typedef int (__cdecl *transparent_fn)(int, int, uint16_t *, const original_graph *, int, int, int);
    opaque_fn opaque = (opaque_fn)(game + 0x55b3a);
    transparent_fn transparent = (transparent_fn)(game + 0x55c52);
    uint16_t source[20];
    for (int i = 0; i < 20; ++i) source[i] = i % 4 == 0 ? 0 : (uint16_t)(0x101 + i);
    const int anchors[3][2] = {{0, 0}, {-2, 1}, {2, -1}};
    FILE *output = fopen("golden\\blit.jsonl", "wb");
    if (!output) fail("open blit vectors");
    for (int anchor = 0; anchor < 3; ++anchor) {
        original_graph graph = {5, 4, (int16_t)anchors[anchor][0], (int16_t)anchors[anchor][1], source};
        for (int mode = 0; mode < 2; ++mode) {
            for (int y = -5; y <= 8; ++y) {
                for (int x = -6; x <= 9; ++x) {
                    uint16_t destination[42 + 16];
                    for (int i = 0; i < 42 + 16; ++i) destination[i] = 0x7bde;
                    int result = mode ? transparent(7, 6, destination, &graph, x, y, 0)
                                      : opaque(7, 6, destination, &graph, x, y);
                    for (int i = 42; i < 42 + 16; ++i)
                        if (destination[i] != 0x7bde) fail("original blit wrote past destination");
                    fprintf(output, "{\"x\":%d,\"y\":%d,\"anchor_x\":%d,\"anchor_y\":%d,\"transparent\":%d,\"return\":%d,\"pixels\":[",
                            x, y, graph.x, graph.y, mode, result);
                    for (int i = 0; i < 42; ++i) fprintf(output, "%s%u", i ? "," : "", destination[i]);
                    fputs("]}\n", output);
                }
            }
        }
    }
    if (ferror(output) || fclose(output)) fail("write blit vectors");
    puts("captured 1344 opaque/transparent blit cases");
}

#include "original_graphics.h"

static void decode_archive(decompress_fn decode, const char *name) {
    char path[MAX_PATH];
    size_t archive_size;
    snprintf(path, sizeof(path), "input\\%s", name);
    unsigned char *archive = read_file(path, &archive_size);
    if (archive_size < 4) fail("missing MKF header");
    uint32_t table = read_u32(archive);
    if (table < 4 || table > archive_size || (archive_size - table) % 4)
        fail("invalid MKF index");
    size_t count = (archive_size - table) / 4;
    snprintf(path, sizeof(path), "decoded\\%s", name);
    make_directory(path);
    for (size_t i = 0; i < count; ++i) {
        uint32_t offset = read_u32(archive + table + i * 4);
        if (offset < 4 || offset > table || table - offset < 16)
            fail("invalid MKF entry position");
        uint32_t unpacked = read_u32(archive + offset);
        uint32_t packed = read_u32(archive + offset + 4);
        if (packed > table - offset - 16 || unpacked > MAX_ENTRY_SIZE)
            fail("invalid MKF entry size");
        const unsigned char *source = archive + offset + 16;
        unsigned char *output = malloc((size_t)unpacked + GUARD_SIZE);
        unsigned char *repeat = malloc((size_t)unpacked + GUARD_SIZE);
        if (!output || !repeat) fail("allocate decoded entry");
        memset(output, 0xa5, (size_t)unpacked + GUARD_SIZE);
        memset(repeat, 0x5a, (size_t)unpacked + GUARD_SIZE);
        if (packed == unpacked) {
            memcpy(output, source, unpacked);
        } else {
            /* No upstream C decoder participates in this oracle. */
            unsigned char *padded = calloc((size_t)packed + 4, 1);
            if (!padded) fail("allocate compressed input");
            memcpy(padded, source, packed);
            decode(output, padded);
            decode(repeat, padded);
            free(padded);
            if (memcmp(output, repeat, unpacked)) fail("decoder left output unwritten");
            for (size_t j = 0; j < GUARD_SIZE; ++j) {
                if (output[unpacked + j] != 0xa5 || repeat[unpacked + j] != 0x5a)
                    fail("decoder exceeded declared output size");
            }
        }
        if (i == 0 && strcmp(name, "data.mkf") == 0 &&
            (unpacked < 12 || memcmp(output, "SMP\0", 4)))
            fail("known original data.mkf[0] SMP signature self-check failed");
        snprintf(path, sizeof(path), "decoded\\%s\\%06lu.bin", name, (unsigned long)i);
        FILE *destination = fopen(path, "wb");
        if (!destination || fwrite(output, 1, unpacked, destination) != unpacked)
            fail("write decoded entry");
        if (fclose(destination)) fail("close decoded entry");
        free(repeat);
        free(output);
    }
    printf("decoded %s: %lu entries\n", name, (unsigned long)count);
    fflush(stdout);
    free(archive);
}

int main(void) {
    /* The launcher verifies the complete EXE SHA-256 before this is invoked. */
    HMODULE game = LoadLibraryExA("input\\rich4.exe", NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) fail("map original executable");
    if ((uintptr_t)game != 0x00400000) fail("original executable not at required image base");
    static const unsigned char prologue[] = {
        0x55, 0x8b, 0xec, 0x53, 0x51, 0x56, 0x57,
        0xbe, 0x30, 0x36, 0x48, 0x00
    };
    unsigned char *entry = (unsigned char *)game + 0x55040;
    if (memcmp(entry, prologue, sizeof(prologue))) fail("unexpected decoder entry");
    printf("original image=%p decoder=%p\n", (void *)game, (void *)entry);
    fflush(stdout);
    make_directory("decoded");
    make_directory("golden");
    decoder_self_check((decompress_fn)entry);
    capture_pixels((unsigned char *)game);
    capture_blits((unsigned char *)game);
    capture_inventory((unsigned char *)game);
    capture_economy((unsigned char *)game);
    capture_calendar((unsigned char *)game);
    capture_direction((unsigned char *)game);
    capture_random((unsigned char *)game);
    capture_spr((unsigned char *)game);
    capture_terrain((unsigned char *)game);
    const char *archives[] = {
        "data.mkf", "effect.mkf", "panel.mkf", "speaking.mkf",
        "help.mkf", "jump.mkf", "map.mkf"
    };
    for (size_t i = 0; i < sizeof(archives) / sizeof(archives[0]); ++i)
        decode_archive((decompress_fn)entry, archives[i]);
    FILE *complete = fopen("complete.txt", "wb");
    if (!complete || fputs("original-mkf-v2-self-checked\n", complete) == EOF || fclose(complete))
        fail("write completion marker");
    /* Do not execute loader detach callbacks for an EXE deliberately mapped
     * without initialization. All streams are already closed before teardown. */
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
