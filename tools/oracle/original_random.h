/* SPDX-License-Identifier: GPL-3.0-or-later
 * Invoke the original Watcom CRT rand/srand; never compute expected RNG values
 * with a reconstructed recurrence in this adapter.
 */
#ifndef RICH4_ORIGINAL_RANDOM_H
#define RICH4_ORIGINAL_RANDOM_H

static uint32_t original_random_context[4];

static void *__cdecl oracle_thread_context(void) {
    return original_random_context;
}

static void capture_random(unsigned char *game) {
    typedef void *(__cdecl *context_fn)(void);
    typedef int (__cdecl *rand_fn)(void);
    typedef void (__cdecl *srand_fn)(uint32_t);
    context_fn *context = (context_fn *)(game + 0x88f4c);
    context_fn previous = *context;
    *context = oracle_thread_context;
    rand_fn next = (rand_fn)(game + 0x56f2d);
    srand_fn seed = (srand_fn)(game + 0x56f50);
    const uint32_t seeds[] = {
        0, 1, 2, 3, 42, 12345, 0x7fffffffU, 0x80000000U,
        0xfffffffeU, 0xffffffffU, 0x12345678U, 0xdeadbeefU
    };
    FILE *output = fopen("golden\\random.csv", "wb");
    if (!output) fail("open original RNG vectors");
    fputs("seed,index,return,state\n", output);
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        memset(original_random_context, 0, sizeof(original_random_context));
        seed(seeds[i]);
        if (original_random_context[3] != seeds[i]) fail("original srand state self-check");
        for (int index = 0; index < 1024; ++index) {
            int value = next();
            if (value < 0 || value > 32767) fail("original rand output range self-check");
            fprintf(output, "%lu,%d,%d,%lu\n", (unsigned long)seeds[i], index, value,
                    (unsigned long)original_random_context[3]);
        }
    }
    if (ferror(output) || fclose(output)) fail("write original RNG vectors");
    *context = previous;
    puts("captured 12288 original Watcom rand outputs and states");
}
#endif
