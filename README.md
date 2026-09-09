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

> **Status:** Tested on **real hardware** — an ESP32 (esp32dev) + HLK-LD2450 on
> UART (GPIO21/22 @ 256000), flashed with ESPHome 2026.5.2 and added to a local
> Home Assistant 2026.2.3. The radar reports real targets (e.g. x=-347 mm,
> y=434 mm, presence on, count=1) and HA registers exactly
> `sensor.ble_kueche_target_1_x … _3_y` and `sensor.ble_kueche_presence_target_count`.
> The frame parser also has host unit tests. The **RMM device-name requirement**
> below was discovered and fixed during that hardware test, and the **Bluetooth
> on/off command was confirmed** (turning it off made the radar's BLE disappear
> from HA). Still not independently confirmed on hardware: multi/single-target and
> factory-reset, and the final "add radar" click inside the RMM UI — see
> [Known limitations](#known-limitations).

---

## Features

- Reads the 30-byte LD2450 data frame (`AA FF 03 00 … 55 CC`) with header resync.
- Per target (1–3): `x`, `y` (mm), `speed` (mm/s), `distance` (mm), `angle` (°),
  `resolution` (mm), plus `present` and `moving` binary sensors.
- Overall `presence` binary sensor (occupancy) and `target_count` sensor.
- Optional **RMM** block: generates `sensor.<radar_name>_target_N_x/_y` and
  `sensor.<radar_name>_presence_target_count`.
- Configuration controls: **Bluetooth** on/off switch, **multi/single-target**
  switch, **restart** and **factory-reset** buttons. Switch states are read back
  from the module (command ACKs + MAC query), never assumed.
- Non-blocking command queue: sequences are queued atomically, each frame waits
  for the module's ACK before the next one goes out, and a rejected or missing
  ACK aborts the sequence and leaves config mode again.
- Only changed values are published (change detection per sensor), with a
  configurable `throttle` for the numeric sensors and optional `filters:` for the
  RMM sensors.

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
esphome:
  name: wohnzimmer-ld2450        # device name MUST slugify to radar_name
  friendly_name: Wohnzimmer LD2450

ld2450_uart:
  id: radar
  uart_id: uart_ld2450
  rmm:
    enabled: true
    radar_name: wohnzimmer_ld2450
    unit: mm
```

> **The device name must match `radar_name`.** Home Assistant prefixes every
> entity_id with the device name (`sensor.<device>_<entity>`), so the component
> gives the RMM sensors short names (`target_1_x`, …) and relies on the device
> name to form the prefix. `radar_name` is therefore optional: when omitted it is
> derived from the device name (`friendly_name`, or `name` if there is none) the
> same way HA slugifies it — including transliteration, so `friendly_name: Küche
> LD2450` gives `kuche_ld2450`. If you set `radar_name` and it does not match,
> the config **fails validation** with a message telling you exactly what to set.
>
> Related rules the validation enforces because HA would otherwise produce
> different ids: `esphome: name_add_mac_suffix: true` is rejected together with
> `rmm:`; only **one** `ld2450_uart:` instance per device may enable `rmm:`; and
> `sensor:` entries for `target_N: x/y` or `target_count` are rejected on a
> component that already generates them via `rmm:` (they would fight over the
> same slot). What the validation cannot see: renaming the device inside Home
> Assistant later changes the prefix of newly registered entities.

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
>
> `filters:` inside `rmm:` appends ordinary ESPHome sensor filters to the six
> coordinate sensors, e.g. `- delta: 10` or `- throttle: 500ms`, to tame the
> recorder load in Home Assistant. Unchanged values are never re-published
> anyway; `throttle` (main component) paces how often changed coordinates go out.

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
| `throttle` | `200ms` | Minimum interval between publishes of the numeric sensors (x/y/speed/distance/angle/resolution). Binary sensors and `target_count` are not throttled; all sensors only publish changed values. `0ms` disables the throttle. |
| `rmm:` | — | Optional RMM sensor auto-generation |

### `rmm:`

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `true` | Generate the RMM sensor set |
| `radar_name` | slug of the device name | `[a-z_][a-z0-9_]*`; must equal the HA slug of the device name (validated) |
| `unit` | `mm` | `mm` or `cm` |
| `filters` | `[]` | Extra sensor filters appended to the six coordinate sensors |

### Platforms

- `sensor:` — `target_1/2/3:` each with `x`, `y`, `speed`, `distance`, `angle`, `resolution`; plus `target_count`. (With `rmm:` enabled, `x`/`y`/`target_count` are generated already and are rejected here.)
- `binary_sensor:` — `presence:` plus `target_1/2/3:` each with `present` / `moving`.
- `switch:` — `bluetooth`, `multi_target`. Both reflect the state read back from the module (`restore_mode` defaults to `DISABLED`); Bluetooth on/off restarts the module to apply.
- `button:` — `restart`, `factory_reset` (factory reset is followed by a restart, which is what makes it take effect).

## Known limitations

- **Tested on hardware (before the command-queue rewrite):** the data path
  (parsing, all per-target sensors incl. angle, presence/present/moving,
  target_count, HA entity registration with correct RMM names) **and** the
  **Bluetooth on/off command** (confirmed: the radar's BLE stopped/started
  advertising). **Not yet confirmed on hardware:** the non-blocking command
  queue, ACK parsing and MAC/tracking-mode read-back introduced afterwards,
  multi/single-target switching and factory-reset — the frames follow the HLK
  protocol / upstream ESPHome `ld2450`, and the ACK parser has host unit tests.
- **RMM end-to-end:** the entities RMM keys on are present in HA with the exact
  expected names, so RMM will discover the radar. The final step of *adding* the
  radar inside the RMM UI is a user action and was not performed/automated.
- **One RMM radar per ESP.** Because HA prefixes every entity with the device
  name, a second `ld2450_uart:` instance on the same ESP cannot enable `rmm:`
  (validation rejects it). Non-RMM instances via `sensor:` are fine.
- `target_count` counts targets where `x != 0 || y != 0`; the per-target
  `moving` binary sensor is derived from `speed != 0`. The LD2450 basic data
  frame carries no separate still/moving target counts.
- Zone filtering, firmware-version and MAC text sensors (present in the upstream
  ESPHome `ld2450` component) are **not** implemented here.
- `unit: cm` rescales by ×0.1 in software; values remain integer-derived.

## Bluetooth note

The LD2450 can be told to turn its Bluetooth radio **off** (or on) **only
because its UART protocol provides a dedicated command** for it
(command word `0x00A4`, value `0x0001`/`0x0000`, wrapped in config mode). This
component exposes that as the `bluetooth` switch. The setting only takes effect
after a module restart, so the switch queues one sequence: enter config mode →
Bluetooth command → restart (`0x00A3`, sent once the Bluetooth command was
acknowledged) → (1.5 s later) enter config mode → MAC query (`0x00A5`) → exit
config mode. Each frame waits for the module's ACK; a rejected or missing ACK
aborts the sequence. The switch state in Home Assistant is published when the
module acknowledges the Bluetooth command and again from the MAC query after the
reboot: the module answers with the sentinel MAC `08:05:04:03:02:01` while
Bluetooth is off, and with its real address while it is on. The same query runs
once after every ESP boot, so the switch shows the module's real state instead
of a remembered one. There is no out-of-band or "force" method: if a
module/firmware did not offer a safe protocol command, Bluetooth could not be
disabled from here.

**Confirmed on hardware:** toggling the `bluetooth` switch off made the LD2450
stop advertising BLE — its separate Bluetooth integration in Home Assistant lost
the device. Toggling back on restores it.

## Acknowledgements / references

- HLK-LD2450 serial protocol; decode logic cross-checked against the upstream
  [ESPHome `ld2450`](https://esphome.io/components/sensor/ld2450/) component.
- [Moe8383/radar_map_manager](https://github.com/Moe8383/radar_map_manager) (RMM).
- [53l3cu5/ESP32_LD2450](https://github.com/53l3cu5/ESP32_LD2450).

## License

[MIT](LICENSE).
