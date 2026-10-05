# SPDX-License-Identifier: GPL-3.0-or-later
"""Additional fixtures for run_stocks_precision_oracle; never used by the game."""
import csv
import hashlib
import struct


def capture(oracle, output, bits):
    # The original CRT requests its thread-state block through this import slot.
    # Supply private guest storage; the original rand/srand arithmetic runs intact.
    oracle.cpu.mem_write(0x10000500, b'\xb8' + struct.pack('<I', 0x10004000) + b'\xc3')
    oracle.write(0x488f4c, 0x10000500)
    outputs = {}

    def save(name, header, rows):
        path = output / name
        with path.open('w') as file:
            writer = csv.writer(file)
            writer.writerow(header)
            writer.writerows(rows)
        outputs[name] = {'rows': len(rows), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        print(name, len(rows), flush=True)

    expected_entries = {0x428d2a: bytes.fromhex("53565783ec0c"),
                        0x42915a: bytes.fromhex("535683ec04"),
                        0x4291d6: bytes.fromhex("53565755"),
                        0x4295ea: bytes.fromhex("83ec10")}
    for address, expected in expected_entries.items():
        if bytes(oracle.cpu.mem_read(address, len(expected))) != expected:
            raise RuntimeError(f"Original entry mismatch: {address:#x}")

    rows = []
    for scenario in range(512):
        seed = (1234567 * (scenario + 1)) & 0xffffffff
        oracle.write(0x1000400c, seed)
        before = []
        for index in range(12):
            available = [0, 1, 999, 1000, 1001, 65535, 16000, 8000][(index + scenario) % 8]
            oracle.write(0x496988 + index * 36, available, 'H')
            before.append(available)
        oracle.call(0x42915a)
        after_seed = oracle.read(0x1000400c)
        for index in range(12):
            rows.append([scenario, seed, index, before[index], oracle.read(0x49698a + index * 36, 'H'), after_seed])
    save('limits.csv', ['case', 'seed', 'index', 'available', 'quota', 'after_seed'], rows)

    rows = []
    # The holiday policy is tested separately. Substitute only its Boolean result
    # so both early exit branches and arbitrary market fixtures are deterministic.
    oracle.cpu.mem_write(0x4523d5, b'\xa1' + struct.pack('<I', 0x10004100) + b'\xc3')
    for scenario in range(1024):
        seed = (0x98765432 + scenario * 1234567) & 0xffffffff
        freeze = 1 if scenario % 29 == 0 else 0
        holiday = 1 if scenario % 31 == 0 else 0
        cursor = scenario % 144
        oracle.write(0x1000400c, seed)
        oracle.write(0x10004100, holiday)
        oracle.write(0x4990dc, freeze)
        oracle.write(0x499100, cursor)
        oracle.write(0x499078, 12345)
        oracle.write(0x4990ec, bits(2.25))
        oracle.clear(0x497328, 12 * 144 * 4)
        oracle.clear(0x496980, 12 * 36)
        oracle.clear(0x10001000, 13 * 52)
        oracle.write(0x498e7c, 0x10001000)
        before = []
        for index in range(12):
            linked = index + 1 if index % 2 else 0
            suspension = (scenario + index) % 7 == 0
            news = [0, 0, 0, 0, 0, 2, 0x20, 0x11][(scenario + index) % 8]
            reference = bits(5 + ((scenario * 19 + index * 47) % 3000) / 10)
            price = bits(1 + ((scenario * 73 + index * 139) % 999800) / 100)
            volatility = bits(0.05 + (index % 5) * 0.31)
            movement = bits(((scenario + index) % 41 - 20) / 2)
            impulse = bits(7.5)
            valuation = 10000 * (30 + index * 50)
            oracle.write(0x496984 + index * 36, linked, 'H')
            oracle.write(0x496986 + index * 36, int(suspension), 'B')
            oracle.write(0x496987 + index * 36, news, 'B')
            for offset, value in [(12, reference), (16, bits(99)), (20, price), (24, volatility), (28, movement), (32, impulse)]:
                oracle.write(0x496980 + index * 36 + offset, value)
            if linked:
                oracle.write(0x10001000 + linked * 52 + 36, valuation)
            before.append([scenario, seed, freeze, holiday, cursor, index, linked, int(suspension), news,
                           reference, price, volatility, movement, impulse, valuation])
        oracle.call(0x4291d6)
        for index in range(12):
            values = [oracle.read(0x496980 + index * 36 + offset) for offset in (16, 20, 28, 32)]
            values += [oracle.read(0x497328 + (index * 144 + cursor) * 4), oracle.read(0x499078),
                       oracle.read(0x4990ec), oracle.read(0x499100), oracle.read(0x1000400c)]
            rows.append(before[index] + values)
    save('daily.csv', ['case', 'seed', 'freeze', 'holiday', 'cursor', 'index', 'linked', 'suspension', 'news',
                      'reference_bits', 'price_bits', 'volatility_bits', 'movement_bits', 'impulse_bits', 'valuation',
                      'previous_after', 'current_after', 'movement_after', 'impulse_after', 'history',
                      'market_index', 'market_movement', 'cursor_after', 'after_seed'], rows)

    rows = []
    for scenario in range(1024):
        player = scenario % 4
        amount = 1 + (scenario * 37 % 1500)
        market = (scenario // 4) % 2
        quote = bits(1 + (scenario * 31 % 999800) / 100)
        basis = bits(1 + (scenario * 13 % 50000) / 100)
        valuation = 10000 * (30 + (scenario % 500)) + 9876
        oracle.clear(0x496980, 12 * 36)
        oracle.clear(0x4971a0, 4 * 96)
        oracle.clear(0x496b68, 4 * 104)
        oracle.clear(0x10001000, 104)
        for slot in range(4):
            oracle.write(0x4971a0 + slot * 96, 1000 - slot * 100)
            oracle.write(0x4971a4 + slot * 96, basis)
            oracle.write(0x496b84 + slot * 104, 100000000)
            oracle.write(0x496b88 + slot * 104, 100000000)
        oracle.write(0x496984, 1, 'H')
        oracle.write(0x496988, 5000, 'H')
        oracle.write(0x49698a, 5000, 'H')
        oracle.write(0x496994, quote)
        oracle.write(0x498e7c, 0x10001000)
        oracle.write(0x10001000 + 52 + 24, 1, 'B')
        oracle.write(0x10001000 + 52 + 28, 0x04030201)
        oracle.write(0x10001000 + 52 + 36, valuation)
        oracle.write(0x10001000 + 52 + 48, 5000)
        oracle.call(0x428d2a, player, 0, amount, market)
        rows.append([scenario, player, amount, market, quote, basis, valuation,
                     oracle.read(0x4971a0 + player * 96), oracle.read(0x4971a4 + player * 96),
                     oracle.read(0x496988, 'H'), oracle.read(0x49698a, 'H'),
                     oracle.read(0x496b84 + player * 104), oracle.read(0x496b88 + player * 104),
                     oracle.read(0x10001000 + 52 + 48), oracle.read(0x10001000 + 52 + 28),
                     oracle.read(0x10001000 + 52 + 24, 'B')])
    save('purchases.csv', ['case', 'player', 'amount', 'market', 'quote_bits', 'basis_before', 'valuation',
                          'amount_after', 'basis_after', 'available', 'quota', 'cash', 'bank', 'primary', 'rank', 'owner'], rows)
    rows = []
    for scenario in range(2048):
        previous = bits(1 + (scenario * 97 % 999800) / 100)
        oracle.write(0x496990, previous)
        variation = scenario % 7
        if variation == 0:
            current = previous
        else:
            delta = [-10, -10, -5, 0, 5, 10, 10][variation]
            current = oracle.call(0x428ec5, previous, bits(delta))
            if variation == 1:
                current = max(bits(1), current - 1)
            if variation == 6:
                current = min(bits(9999), current + 1)
        oracle.write(0x496994, current)
        rows.append([previous, current, oracle.call(0x4295ea, 0)])
    save('trends.csv', ['previous_bits', 'current_bits', 'trend'], rows)
    return outputs
