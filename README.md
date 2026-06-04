# esphome-ld2450-uart-rmm

An [ESPHome](https://esphome.io/) **external component** for the HLK-**LD2450**
24 GHz mmWave radar over **UART**, with optional one-switch
[Radar Map Manager (RMM)](https://github.com/Moe8383/radar_map_manager)
compatibility.

It reads the LD2450 target-tracking serial stream and exposes, for up to **3
targets**, their X/Y position, speed, distance and resolution — plus a presence
binary sensor and a target-count sensor. When the `rmm:` option is enabled it
auto-creates sensors named exactly the way RMM expects, so a radar shows up in
RMM with no manual entity renaming.

> **Status:** YAML/codegen validated and firmware-compiled with ESPHome 2026.5.2;
> the frame parser is covered by host unit tests. **It has not yet been run on
> real hardware** — see [Known limitations](#known-limitations).

---

## Features

- Reads the 30-byte LD2450 data frame (`AA FF 03 00 … 55 CC`) with header resync.
- Per target (1–3): `x`, `y` (mm), `speed` (mm/s), `distance` (mm), `resolution` (mm).
- `presence` binary sensor (occupancy) and `target_count` sensor.
- Optional **RMM** block: generates `sensor.<radar_name>_target_N_x/_y` and
  `sensor.<radar_name>_presence_target_count`.
- Configuration controls: **Bluetooth** on/off switch, **multi/single-target**
  switch, **restart** and **factory-reset** buttons.
- Configurable publish `throttle`.

## Installation (external_components)

Point ESPHome at this repository. Use whichever source suits you:

```yaml
# From GitHub
external_components:
  - source: github://magicx78/esphome-ld2450-uart-rmm
    components: [ld2450_uart]

# …or from a local checkout (as the bundled examples do)
external_components:
  - source:
      type: local
      path: components
    components: [ld2450_uart]
```

## UART wiring (ESP32)

| LD2450 | ESP32 |
|--------|-------|
| TX     | GPIO16 (ESP **RX**) |
| RX     | GPIO17 (ESP **TX**) |
| VCC    | 5V |
| GND    | GND |

The LD2450 runs at **256000 baud, 8N1** by default. A **hardware UART** is
strongly recommended at that speed. Free the pins from the logger with
`logger: { baud_rate: 0 }` when using the default UART0 pins, or use a separate
UART as in the examples.

```yaml
uart:
  id: uart_ld2450
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 256000
  parity: NONE
  stop_bits: 1

ld2450_uart:
  id: radar
  uart_id: uart_ld2450
  throttle: 200ms
```

## Examples

Three ready-to-validate configs live in [`examples/`](examples/):

### Basic — [`examples/basic.yaml`](examples/basic.yaml)

Raw target X/Y/speed/distance/resolution sensors, presence and target count with
your own entity names.

### RMM — [`examples/rmm.yaml`](examples/rmm.yaml)

```yaml
ld2450_uart:
  id: radar
  uart_id: uart_ld2450
  rmm:
    enabled: true
    radar_name: wohnzimmer_ld2450
    unit: mm
```

This produces exactly:

```
sensor.wohnzimmer_ld2450_target_1_x   sensor.wohnzimmer_ld2450_target_1_y
sensor.wohnzimmer_ld2450_target_2_x   sensor.wohnzimmer_ld2450_target_2_y
sensor.wohnzimmer_ld2450_target_3_x   sensor.wohnzimmer_ld2450_target_3_y
sensor.wohnzimmer_ld2450_presence_target_count
```

In RMM, add a radar named **`wohnzimmer_ld2450`** (RMM derives the name by
stripping `_presence_target_count` from that entity). RMM's documented ESPHome
naming convention is `sensor.[radar_name]_target_?_x` / `_y` and
`sensor.[radar_name]_presence_target_count`, which is what the block generates.

> `unit: mm` is the native unit. `unit: cm` additionally applies a `×0.1` filter
> and labels the sensors in cm. RMM normally expects mm.

### Full — [`examples/full.yaml`](examples/full.yaml)

RMM sensors + extra per-target speed/distance/resolution + the Bluetooth /
multi-target switches and restart / factory-reset buttons.

## Testing in a local Home Assistant dev server

This repo was developed and validated against a local HA + ESPHome CLI setup
(WSL Ubuntu, ESPHome 2026.5.2, Home Assistant 2026.2.3):

```bash
# YAML + codegen validation (no hardware needed)
esphome config examples/basic.yaml
esphome config examples/rmm.yaml
esphome config examples/full.yaml

# Full ESP32 firmware build
esphome compile examples/rmm.yaml

# Host-side parser unit test (no ESPHome / no hardware)
g++ -std=c++17 -Icomponents/ld2450_uart tests/test_parser.cpp -o test_parser
./test_parser
```

For an end-to-end HA test you then flash an ESP32, add the device via the
ESPHome integration, and confirm the entities appear. With `rmm:` enabled, RMM
should auto-discover the radar by its `radar_name`. (This live step needs real
hardware — see below.)

## Configuration reference

### `ld2450_uart:` (main component)

| Option | Default | Description |
|--------|---------|-------------|
| `uart_id` | — | UART bus to use (256000 8N1) |
| `throttle` | `200ms` | Minimum interval between publishes |
| `rmm:` | — | Optional RMM sensor auto-generation |

### `rmm:`

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `true` | Generate the RMM sensor set |
| `radar_name` | — *(required)* | Lowercase `[a-z0-9_]+`; becomes the entity_id prefix |
| `unit` | `mm` | `mm` or `cm` |

### Platforms

- `sensor:` — `target_1/2/3:` each with `x`, `y`, `speed`, `distance`, `resolution`; plus `target_count`.
- `binary_sensor:` — `presence` (the platform entry itself).
- `switch:` — `bluetooth`, `multi_target`.
- `button:` — `restart`, `factory_reset`.

## Known limitations

- **No hardware test yet.** The component validates, compiles and passes the
  host parser tests, but it has **not** been run against a physical LD2450 +
  ESP32. Live target values, the config commands (Bluetooth/restart/etc.) and
  RMM's live discovery are therefore **unverified on real hardware**.
- `target_count` counts targets where `x != 0 || y != 0`; the LD2450 does not
  distinguish "still" vs "moving" in the basic data frame, so that split is not
  exposed.
- Zone filtering, firmware-version and MAC text sensors (present in the upstream
  ESPHome `ld2450` component) are **not** implemented here.
- `unit: cm` rescales by ×0.1 in software; values remain integer-derived.

## Bluetooth note

The LD2450 can be told to turn its Bluetooth radio **off** (or on) **only
because its UART protocol provides a dedicated command** for it
(command word `0x00A4`, value `0x0001`/`0x0000`, wrapped in enter/exit config
mode). This component exposes that as the `bluetooth` switch. There is no
out-of-band or "force" method — if a future module or firmware did not offer a
safe protocol command, Bluetooth could not be disabled from here. The command
bytes are implemented per the HLK serial protocol but have **not been confirmed
against a real module** (no hardware).

## Acknowledgements / references

- HLK-LD2450 serial protocol; decode logic cross-checked against the upstream
  [ESPHome `ld2450`](https://esphome.io/components/sensor/ld2450/) component.
- [Moe8383/radar_map_manager](https://github.com/Moe8383/radar_map_manager) (RMM).
- [53l3cu5/ESP32_LD2450](https://github.com/53l3cu5/ESP32_LD2450).

## License

[MIT](LICENSE).
