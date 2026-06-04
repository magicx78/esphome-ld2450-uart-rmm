# Freshness Status
Letzter Check: 2026-06-04

| Domain | Referenz | Installiert/Aktuell | Status |
|--------|----------|---------------------|--------|
| ESPHome | dev docs (esphome.io) | 2026.5.2 (venv) | ✅ AKTUELL |
| Home Assistant | — | 2026.2.3 (ha-dev) | ✅ AKTUELL |
| LD2450 Protokoll | HLK serial + esphome `ld2450` (dev) | — | ✅ verifiziert |
| RMM | Moe8383/radar_map_manager (geklont) | local | ✅ verifiziert |

## Verifizierte Fakten (Quelle: esphome/esphome dev + RMM repo)
- Datenframe: `AA FF 03 00` + 3×8 Byte + `55 CC` = 30 Byte.
- Koordinate: `(high&0x7F)<<8 | low`; negativ wenn Top-Bit von high = 0.
- Speed: gleiche Vorzeichenregel, ×10 → mm/s.
- Kommandoframe: `FD FC FB FA | len_lo len_hi | cmd 00 | value | 04 03 02 01`.
  Worte: enter 0xFF(val 0001), exit 0xFE, restart 0xA3, factory 0xA2,
  bluetooth 0xA4(0001/0000), single 0x80, multi 0x90, version 0xA0.
- ESPHome→HA entity_id = `<device_name>_<entity_name>` (slugified). Das Geräte-
  Präfix wird von HA IMMER vorangestellt (bestätigt auf echter Hardware:
  Sensor-Name `target_1_x` am Gerät `ble-kueche` → `sensor.ble_kueche_target_1_x`).
  Präfix kommt aus friendly_name falls gesetzt, sonst node name.
  → RMM-Sensoren bekommen KURZE Namen (`target_1_x`), und der Geräte-Name MUSS
  == radar_name sein (per FINAL_VALIDATE_SCHEMA erzwungen). Frühere Annahme
  „kein Präfix" war FALSCH — erst der Hardware-Test deckte es auf.
- RMM-Erkennung: scannt `sensor.<name>_presence_target_count`, Suffix abschneiden
  → radar_name; erwartet `sensor.<radar_name>_target_N_x/_y`
  (Quelle: radar_map_manager README + www/radar-editor.js).

## Codegen-Stolperstein (gelöst)
- In `__init__.py` eines Pakets mit `sensor.py`-Submodul: `from esphome.components
  import sensor` wird beim Laden des Submoduls überschrieben (Paket-Namespace).
  → als `core_sensor` aliasen.
