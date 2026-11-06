# Reference documents

Vendor datasheets and schematics are **not committed** to the repository: they
are large and not ours to redistribute. They live in the workspace root, outside
this repository:

| File | Location | SHA-256 |
| --- | --- | --- |
| ESP32-P4 datasheet (PDF) | `../../../p4_datasheet.pdf` | `fb4f3e91cc2ac519ec08cdec3dac9cb62b546cf5402ead1c5b83595a21bb6bc6` |
| ESP32-P4 datasheet (text) | `../../../p4_datasheet.txt` | `8f83adb79bfa0fe838cd86280a4de933c41699c41691da6a179afc153521c271` |
| Board schematic (PDF) | `../../../schematic.pdf` | `d577a6a8d4caafceea1e6770c8877c32db88a8359c67e2285de2ed9dc156dc21` |
| Board schematic (text) | `../../../schematic_text.txt` | `ffb7fa58ca64e09d68589db4d99f1618763df92a6c21f513e96102ed14dbb220` |

`docs/reference/*` is gitignored except this file.

## Sources

- Product documentation: <https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B>
- ESP-IDF guide: <https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B/ESP-IDF>
- FAQ: <https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B/FAQ>
- Resources/schematic: <https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B/Resources-And-Documents>
- Official repository: <https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B>
- BSP 3.0.1: <https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_7b/versions/3.0.1>
- BSP source: <https://github.com/waveshareteam/Waveshare-ESP32-components>

## Notes

- The text extractions were used to search nets and pin names; the PDFs are the
  authoritative copies.
- If these files are moved, update the table. Do not add them to git.
- When porting to another board, replace this reference set; the rest of the
  handbook stays valid.
