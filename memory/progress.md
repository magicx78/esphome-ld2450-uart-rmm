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

## Bewusst offen (keine Hardware)
- Kein realer LD2450/ESP32 → keine Live-Werte, keine RMM-Live-Erkennung,
  Kommando-Frames (BT/Restart/…) nicht am Modul gegengetestet.

## Wichtige Details
Siehe `memory/freshness.md` für Protokoll- und Codegen-Fakten.
