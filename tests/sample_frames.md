# LD2450 Sample Frames & Decoding Reference

This documents the frame format the parser in `components/ld2450_uart/ld2450_protocol.h`
implements, and the simulated frames used by `tests/test_parser.cpp`.

## Data (report) frame — 30 bytes

```
AA FF 03 00 | T1 (8 bytes) | T2 (8 bytes) | T3 (8 bytes) | 55 CC
```

- Header: `AA FF 03 00`
- Tail: `55 CC`
- Each target = 8 bytes: `X(2) Y(2) Speed(2) Resolution(2)`, little-endian byte pairs.

### Coordinate / speed sign convention

For X, Y and Speed the value is encoded as:

```
value = ((high & 0x7F) << 8) | low
if (high & 0x80) == 0:   value = -value     # top bit CLEAR => negative
```

- X, Y are in **mm**.
- Speed raw is in cm/s and is multiplied by 10 → reported in **mm/s**.
- Resolution = `(high << 8) | low` (mm, no sign bit).
- A target is considered **active** when `x != 0 || y != 0`. An empty slot is all
  zero bytes (`00 00 00 00 00 00 00 00`).

## Worked examples (asserted in test_parser.cpp)

| Case | Target 1 (x mm, y mm, speed mm/s, res) | count |
|------|----------------------------------------|-------|
| Empty frame (all slots zero) | — | 0 |
| Single positive | (1000, 1500, 200, 240) | 1 |
| Negative coords/speed | (-500, -2000, -150, 100) | 1 |
| Three targets | (100,200,50,50) (-300,400,-100,60) (1234,-567,300,70) | 3 |

The test builds each frame by applying the *inverse* of the decode rule
(`encode_signed`) and then asserts the parser recovers the original values, so it
exercises the exact byte order and sign-bit handling.

## Command frame

```
FD FC FB FA | len_lo len_hi | cmd_word(2 LE) | value... | 04 03 02 01
```

Command words used by this component (low byte; high byte 0x00):

| Action | Word | Value |
|--------|------|-------|
| Enter config mode | 0x00FF | 0x0001 |
| Exit config mode | 0x00FE | — |
| Restart module | 0x00A3 | — |
| Factory reset | 0x00A2 | — |
| Bluetooth on/off | 0x00A4 | 0x0001 / 0x0000 |
| Single-target tracking | 0x0080 | — |
| Multi-target tracking | 0x0090 | — |

> The command-frame **bytes are implemented and unit-checkable, but the
> round-trip against a real module has not been tested** (no hardware available).
> See the README "Known limitations" section.
