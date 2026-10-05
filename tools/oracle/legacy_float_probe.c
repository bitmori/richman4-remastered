/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent x86_64 x87 probe, containing no game implementation or bytes.
 * Compile on Apple Silicon with clang -arch x86_64, run via Rosetta and save
 * stdout to a private CSV. Only test code contains assembly; the shared runtime
 * implementation uses integer C++ and is tested separately on native ARM.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t bits64(double value) { uint64_t raw; memcpy(&raw, &value, 8); return raw; }
static uint32_t bits32(float value) { uint32_t raw; memcpy(&raw, &value, 4); return raw; }
static uint32_t next(uint32_t* seed) { *seed = *seed * 1664525U + 1013904223U; return *seed; }

static void probe(unsigned operation, double a, double b, double c) {
    const uint16_t nearest = 0x037f, toward_zero = 0x0f7f;
    float single;
    double twice;
    int32_t integer;
#define RESULT "fstl %[twice]; fsts %[single]; fldcw %[zero]; fistpl %[integer]; fldcw %[nearest]"
#define OUTPUTS [twice] "=m"(twice), [single] "=m"(single), [integer] "=m"(integer)
#define INPUTS [a] "m"(a), [b] "m"(b), [c] "m"(c), [zero] "m"(toward_zero), [nearest] "m"(nearest)
    switch (operation) {
    case 0:
        __asm__ volatile("fldcw %[nearest]; fldl %[a]; faddl %[b]; fmull %[c]; " RESULT : OUTPUTS : INPUTS : "st");
        break;
    case 1:
        __asm__ volatile("fldcw %[nearest]; fldl %[a]; fsubl %[b]; fdivl %[c]; " RESULT : OUTPUTS : INPUTS : "st");
        break;
    case 2:
        __asm__ volatile("fldcw %[nearest]; fldl %[a]; fmull %[b]; faddl %[c]; " RESULT : OUTPUTS : INPUTS : "st");
        break;
    default:
        __asm__ volatile("fldcw %[nearest]; fldl %[a]; fdivl %[b]; fsubl %[c]; " RESULT : OUTPUTS : INPUTS : "st");
        break;
    }
    printf("%u,%llu,%llu,%llu,%u,%llu,%u\n", operation,
           (unsigned long long)bits64(a), (unsigned long long)bits64(b), (unsigned long long)bits64(c),
           bits32(single), (unsigned long long)bits64(twice), (uint32_t)integer);
}

int main(void) {
    puts("operation,a_bits,b_bits,c_bits,single_bits,double_bits,integer_bits");
    uint32_t seed = 0x87654321;
    for (unsigned index = 0; index < 10000; ++index) {
        const uint32_t raw = next(&seed);
        int32_t signed_value;
        memcpy(&signed_value, &raw, 4);
        const double a = signed_value;
        const double b = (float)(1.0 + (next(&seed) % 999800) / 100.0);
        const double c = (float)(1.0 / (1 + next(&seed) % 1000));
        for (unsigned operation = 0; operation < 4; ++operation) {
            probe(operation, a, b, c);
        }
    }
    return ferror(stdout) ? 1 : 0;
}
