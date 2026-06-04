# Progress — esphome-ld2450-uart-rmm

Stand: 2026-06-04

## Ziel
Neue eigenständige ESPHome External Component für HLK-LD2450 über UART, mit
optionaler RMM-kompatibler Sensor-Namensgebung. Eigenes GitHub-Repo.

## Status
- [x] Projektscaffold unter `C:\Users\magic\Documents\claude\esphome-ld2450-uart-rmm`
- [x] C++ Kern: `ld2450_protocol.h` (pure, testbar) + `ld2450_uart.h/.cpp`
- [x] Python-Codegen: `__init__.py` (+ `rmm:`), `sensor/binary_sensor/switch/button.py`
- [x] Beispiele: `basic.yaml`, `rmm.yaml`, `full.yaml`
- [x] Parser-Test: `tests/test_parser.cpp` → 28/28 Checks grün (g++)
- [x] `esphome config` basic/rmm/full → alle VALID (ESPHome 2026.5.2)
- [x] RMM-Namen korrekt generiert (esphome config verifiziert)
- [x] `esphome compile examples/rmm.yaml` → SUCCESS (firmware.bin, Flash 12.2%)
- [x] README / LICENSE (MIT) / .gitignore
- [x] git init + initial commit (branch main)
- [x] GitHub-Repo erstellt + gepusht: https://github.com/magicx78/esphome-ld2450-uart-rmm

## Hardware-Test (2026-06-04) — DURCHGEFÜHRT
- ESP32 (esp32dev, MAC a4:e5:7c:fb:50:28) + LD2450 an UART GPIO21/22 @256000.
- Geflasht via ESPHome 2026.5.2 (USB→WSL via usbipd, /dev/ttyACM0).
- Echte Radarwerte bestätigt (API): target_1_x=-347, target_1_y=434, presence=on, count=1.
- In HA (2026.2.3) aufgenommen → exakt sensor.ble_kueche_target_1_x … _3_y +
  _presence_target_count. Alte doppelte Entities von ESPHome-Integration auto-entfernt.
- **Bug gefunden & gefixt**: HA stellt entity_id immer Geräte-Name voran →
  rmm: nutzt jetzt KURZE Sensor-Namen + FINAL_VALIDATE erzwingt Geräte-Name==radar_name.

## Noch offen
- UART-Config-Kommandos (BT/Restart/Factory/Multi) nicht am Modul gegengetestet.
- RMM-UI-„Radar hinzufügen" ist eine Nutzeraktion (nicht automatisiert); Entities
  sind aber korrekt da, RMM-Discovery-Voraussetzung erfüllt.

## Wichtige Details
Siehe `memory/freshness.md` für Protokoll- und Codegen-Fakten.
