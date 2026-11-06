# DSI probe

Raw `esp_lcd` MIPI-DSI stress test for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B.
It bypasses LVGL, touch, audio and Wi-Fi and continuously draws 50-line stripes
into the panel's DSR framebuffers, waiting for the DPI completion callback.

Use it to separate a panel/DSI/PSRAM/power fault from an LVGL/adaptor fault.

Verified on this unit:

- 70 seconds at ~66 fps
- zero completion timeouts
- 3 DPI framebuffers, 1024x600 RGB565, 60% backlight

Build (ESP-IDF v5.5.5, rev1.3 board):

```bash
idf.py -B build \
  -D SDKCONFIG="$PWD/sdkconfig.rev1_3" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1_3.defaults" \
  set-target esp32p4 build
idf.py -B build -D SDKCONFIG="$PWD/sdkconfig.rev1_3" \
  -p /dev/cu.usbmodemXXXX flash monitor
```

Expected serial output every 30 frames:

```text
dsi_probe: frame=30 fps=66.1 timeouts=0 int=... psram=...
```
