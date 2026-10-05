/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent x86 instruction probe. On Apple Silicon, compile with
 * clang -arch x86_64 tools/oracle/fprem_probe.c -o /tmp/rich4-fprem-probe
 * and execute through Rosetta. This contains no original game bytes.
 */
#include <math.h>
#include <stdio.h>

int main(void) {
    const double dividend = 1.5;
    const double divisor = 0.05;
    double actual;
    __asm__ volatile("fldl %2; fldl %1; fprem; fstpl %0; fstp %%st(0)"
                     : "=m"(actual) : "m"(dividend), "m"(divisor) : "st");
    const double expected = fmod(dividend, divisor);
    printf("x87 FPREM %.18g; binary64 fmod %.18g\n", actual, expected);
    return actual == expected && actual > 0.049 ? 0 : 1;
}
