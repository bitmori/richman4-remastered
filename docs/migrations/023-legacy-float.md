# 023 — Shared original floating-point semantics

`include/rich4/legacy_float.hpp` extracts the tested stock arithmetic into the
header-only `rich4::numeric::Extended` type. This lets card and event handlers
preserve x87 evaluation and explicit memory stores without copying assembly or
assuming an x86 host. It adds no external dependency and changes no stock rule.

`from(double)` exactly imports an IEEE binary64 value. Original int32 values and
binary32 values can be passed directly because their conversion to double is
exact. Callers must perform original 32-bit wrapping integer operations before
constructing `Extended`; passing an arbitrary int64/uint64 through a double
would not preserve more than 53 significant bits.

Addition, subtraction, multiplication and division round to a 64-bit
significand, nearest with ties to even, as the original x87 control word
`0x037f`. Operations use `uint64_t` and Clang/GCC `__uint128_t`; they never change
a host thread's rounding mode. Explicit `.single()` and `.double_precision()`
model binary32 and binary64 stores. `.integer()` truncates to signed32,
returning `INT32_MIN` on conversion overflow, matching the original masked x87
integer-indefinite result.

This distinction matters beyond stock quotes: some original card handlers
store a double but pass its low 32 bits to an integer parameter. Callers can
preserve that existing behavior explicitly with `std::bit_cast<uint64_t>` and
a fixed-width extraction after `.double_precision()`. The numeric helper does
not conceal the reinterpretation inside a normal numeric conversion.

## Scope and boundaries

This is a finite arithmetic helper for the migrated game formulas, not a full
x87 emulator. NaN/infinity inputs and division by zero throw. Binary32/64
stores require zero or normal finite results; subnormal stores and overflow
throw. Signed zero, exception status flags, other precision-control modes and
other rounding modes are not modeled. The current stock/card formulas remain
inside this domain. MSVC needs an equivalent wide-integer implementation
before this header can be used there; the project's macOS/Android Clang and
Linux GCC/Clang support the chosen extension.

## Independent validation

`tools/oracle/legacy_float_probe.c` contains independent x86_64 **test-only**
x87 sequences for `(a+b)*c`, `(a-b)/c`, `(a*b)+c`, and `(a/b)-c`. It uses control
word `0x037f` for arithmetic/stores and `0x0f7f` for integer truncation, and
records actual binary32, binary64 and signed32 results. It contains no Rich4
machine code, game asset, or implementation of `Extended`.

On the development Apple Silicon Mac the probe runs as x86_64 through Rosetta.
The ARM-native C++ test compares all three outputs of **40,000 expressions**:
**0 mismatches**, with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
This is independent instruction evidence, distinct from the corrected
Unicorn FPREM stock oracle described in migration 015. It is not described as
a physical Intel processor run.

Unit cases separately check cancellation beyond double precision, halfway
rounding at binary32/binary64/x87 precision, sign handling, integer-indefinite
and defined invalid-input boundaries. Extracting the implementation also
reruns the complete stock oracle suite; every previously matched record
remains equal.

```sh
clang -arch x86_64 -Wall -Wextra -Werror tools/oracle/legacy_float_probe.c \
  -o .local/stocks-oracle/legacy-float-probe
.local/stocks-oracle/legacy-float-probe > .local/stocks-oracle/x87-probe.csv
build/native/numeric_tests .local/stocks-oracle/x87-probe.csv
build/native/stocks_tests .local/stocks-oracle/precision-latest
```

The production helper and synthetic tests are GPL-3.0-or-later like the rest
of this migration. Only the diagnostic probe uses assembly; production C++
works on the native 64-bit ARM build without Wine or an x86 emulator.
