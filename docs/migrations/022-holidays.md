# Original holiday rules and bank loan due dates

`rich4::holidays` migrates the original bank/stock-market closure decision and
90-day loan due-date calculation. The source is mytbk/rich4 revision
`ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`: holiday functions `0x4521f0` and
`0x4523d5` in `asm/rich4.asm`, and loan helper `0x433b7e` in
`asm/rich4_ui_bank.asm`. Existing portable `calendar` arithmetic retains the
original divisible-by-four leap rule.

## Tables and exact supported range

The recovered lunar lookup begins at original VA `0x47639c`. It contains exactly
8401 packed dates indexed from Gregorian 1998-01-01 through 2020-12-31 inclusive.
The first entry is lunar 1997-12-03; the last is lunar 2020-11-17. The two final
values cross an assembly label boundary at `0x47e6d8`; stopping at the preceding
blank line would incorrectly lose them. The following word at `0x47e6e0` is
`0x193a3f08`, unrelated data, not another lunar date.

The portable source stores the exact data as 285 runs of consecutive days within
a lunar month. This is lossless compression of the recovered table, not a lunar
calendar algorithm or an extrapolation. `lunar_date` returns an optional original
packed date. The entire 8401-entry expansion is compared to the user's original
EXE during the local differential run.

The holiday table at `0x47ff4a` has eight regions, 24 rows per region and 12 bytes
per row. Only the five scalar fields needed by the rule are migrated: flags,
calendar kind, month, day or weekday occurrence, and weekday. Text/sprite/voice
fields are not copied. Region selection is `stage*4+map`, with map 0–3 and stage
0–1. Rows are checked in original order: the first matching row without flag
`0x80` determines whether its nonzero flag closes business. A matching zero flag
is an observance without a closure. Sunday always closes business; Saturday is
otherwise governed by those rows.

Within the original lookup range, the translation also retains less obvious
machine behavior. The comparison register keeps a lunar month/day after a lunar
row. The nth-weekday branch replaces an earlier requested weekday with 7 rather
than adding 7 to that weekday. These behaviors are not silently normalized into
modern calendar rules; the whole original date/map comparison validates them.

## Explicit post-2020 correction

An original 2026 game indexes past the end of the lunar table and reads adjacent
static data. Reproducing that out-of-bounds read would neither provide a valid
lunar date nor be an acceptable portable memory access.

For dates after 2020-12-31 the portable decision therefore uses **the original
fixed Gregorian holiday rows and Sunday only**. It skips lunar and nth-weekday
rows. This fallback is intentionally limited: it does not claim a reliable
modern lunar conversion, updated holiday legislation, or byte-identical 2026
holiday behavior to the Windows process. A future calendar extension can be a
separate documented improvement. The current fallback lets loans and daily
market progression continue safely on the user's current date.

Dates before 1998 or invalid calendar dates remain rejected by the original
portable calendar domain. Invalid map/stage indices are rejected before table
access. Date arithmetic that passes the maximum supported year also reports an
error instead of wrapping into an invalid lookup.

## API and integration

`is_closed(date,map,stage)` supplies the bank/stock closure decision.
`loan_due_date(current,map,stage)` adds exactly 90 days and advances past closed
dates. The original helper first checks whether a player already has a due date;
the session preserves that existing date and calls this pure calculation only
when establishing a new one. The module does not modify a player, read a system
clock, use a PRNG or make assumptions about cash/loan eligibility.

## Validation

Default tests cover first/last lookup values, original Lunar New Year transitions,
Saturday/Sunday behavior, original fixed holidays, post-2020 safety, absence of
guessed lunar/nth-weekday closures, invalid dates/indices, and 2026 loans crossing
fixed holidays and Sundays. They pass under AddressSanitizer and
UndefinedBehaviorSanitizer with strict conversion warnings.

The optional original-machine-code comparison is reproducible with
`scripts/run_holidays_oracle.py --native-test /path/to/rich4-holidays-tests` using
Python with Unicorn 2.1.4. The adapter verifies the user's EXE SHA-256, maps its PE
sections read-only as input into a private emulator, runs its original integer
instructions without function patches/import stubs/result overrides, and checks
New Year and open-Saturday self-checks before capture. The emulator's writable
state is private; the user's original file and game process are not altered.

The completed local run matched all **67,208 holiday decisions** (8401 dates on
eight maps), every original lunar table value, and **2,184 original loan due
dates** (all maps, monthly starts from January 1998 through September 2020).
The native reader rejects missing, duplicate, reordered or additional fixture
rows; an empty comparison cannot pass. The capture manifest records EXE/adapter
hashes and exact output row counts/hashes in ignored `.local/holidays-oracle`.
No original executable, commercial save, artwork, or captured asset is committed.
The post-2020 fallback has separate tests and is explicitly excluded from these
original-code equivalence counts.
