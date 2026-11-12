# GPIO Pinout: AI-Thinker ESP32-CAM

This firmware targets the AI-Thinker ESP32-CAM module. This board exposes a
limited set of free GPIOs once the camera and its strapping-pin quirks are
accounted for, this table is the single source of truth for every pin this
project uses. Cross-check any code change against this file before wiring
anything.

## Reserved by the camera module (`src/camera_pins.h`, do not reuse)

| GPIO | Signal | Notes |
|---|---|---|
| 32 | PWDN | Camera power-down control |
| - | RESET | Not connected on this board (`-1`) |
| 0 | XCLK | Camera clock input. Also a boot-mode strapping pin, must be free of external pull during normal boot |
| 26 | SIOD | Camera I2C (SCCB) data |
| 27 | SIOC | Camera I2C (SCCB) clock |
| 35 | Y9 (D7) | Camera data bus |
| 34 | Y8 (D6) | Camera data bus |
| 39 | Y7 (D5) | Camera data bus |
| 36 | Y6 (D4) | Camera data bus |
| 21 | Y5 (D3) | Camera data bus |
| 19 | Y4 (D2) | Camera data bus |
| 18 | Y3 (D1) | Camera data bus |
| 5  | Y2 (D0) | Camera data bus |
| 25 | VSYNC | Camera frame sync |
| 23 | HREF | Camera line sync |
| 22 | PCLK | Camera pixel clock |

## Also reserved (board-level constraints, not camera pins)

| GPIO | Reason | Notes |
|---|---|---|
| 1 | UART0 TXD | Used by Serial/flashing, do not repurpose |
| 3 | UART0 RXD | Used by Serial/flashing, do not repurpose |
| 16 | PSRAM | Used internally for PSRAM on this module's chip revision, not available as a general-purpose pin |

## Available GPIOs used by this project

| GPIO | Used by | File | Notes |
|---|---|---|---|
| 13 | Pan servo | `src/servo_control.cpp` | Safe general-purpose output, no boot-strapping role |
| 15 | Tilt servo | `src/servo_control.cpp` | Strapping pin for JTAG selection, but safe to drive as an output once boot has completed; not loaded externally during boot in this wiring |
| 14 | Door-lock servo | `src/servo_control.cpp` | Safe general-purpose output |
| 2 | Buzzer (LEDC tone) | `src/local_alert.cpp` | Strapping pin, must be LOW or floating during boot for normal boot mode; a servo motor or buzzer driver on this pin does not present a conflicting pull at power-on in this wiring, but confirm during Juan's physical bring-up (see docs/BRINGUP.md step 4) |
| 4 | Status/alert RGB LED (WS2812, data pin) | `src/local_alert.cpp` | Same pin as the board's onboard flash LED, but driven as an external WS2812 data line instead: color-coded status that shows the device step (connecting, portal, online, alert, fatal, button reset) instead of a plain white flash. See "Status LED (WS2812)" below |
| 12 | Reset and pairing button (to GND, INPUT_PULLUP) | `src/reset_button.cpp` | MTDI strapping pin. A button between GPIO12 and GND holds it LOW, which is the safe level at power-on. See "GPIO12 reset button and the flash voltage strapping" below |

### GPIO12 reset button and the flash voltage strapping

GPIO12 (MTDI) is a strapping pin. The chip samples it at reset: LOW selects
3.3V flash I/O, which is what this board needs, and HIGH selects 1.8V flash I/O
and the board fails to boot. This is the reason the pin used to be listed as
unused, and that earlier statement (never used, never wired) was removed on
purpose.

The reset button sits between GPIO12 and GND, so it only ever pulls the pin
LOW. Pressing it, or holding it while the board powers on, is therefore safe,
and that is exactly what the hold-during-boot reset relies on. The firmware
enables the internal pull-up (`INPUT_PULLUP`) only after it starts, so nothing
holds the pin HIGH at reset.

Never add any of these on GPIO12, each one pulls the pin HIGH at reset and
stops the board from booting:

- an external pull-up resistor to 3.3V,
- an active-high button wired to 3.3V,
- a microSD card in the slot (GPIO12 is SD_DATA2 there), keep the slot empty.

Behavior (details in `docs/WIRING.md`): a short press only logs, a hold of 3 s
erases the saved config and restarts into the setup portal.

## Status LED (WS2812) on GPIO4

The AI-Thinker ESP32-CAM has a bright white LED soldered next to the camera
on GPIO4, normally used as a photography flash. This project does not use
that onboard LED: GPIO4 instead drives an external single WS2812/NeoPixel
RGB LED (`Adafruit_NeoPixel`, `src/local_alert.cpp`), wired per
`docs/WIRING.md`. Same pin, same zero extra GPIO cost as reusing the
onboard LED would have been, but color-coded status instead of a plain
white flash:

| Pattern | Meaning | Set from |
|---|---|---|
| Slow blue blink | WiFi connecting or reconnecting (saved credentials) | `wifi_manager.cpp` |
| Pulsing cyan | Setup portal open, no phone joined yet | `provisioning.cpp` |
| Solid cyan | A phone joined the SecurIoT-Setup network | `provisioning.cpp` |
| Pulsing yellow | WiFi credentials accepted, waiting for `/provision-extra` | `provisioning.cpp` |
| Green | WiFi connected, idle (no active alert) | `wifi_manager.cpp`, `local_alert.cpp` |
| Red (steady) | Confirmed detection / active alert | `local_alert.cpp` |
| Red (blinking, forever) | Fatal error (camera init failed) | `main.cpp` |
| Magenta blink that speeds up | Reset button held, keep holding | `reset_button.cpp` |
| Fast white blink | Reset confirmed, release the button | `reset_button.cpp` |
| Solid orange | Firmware update over WiFi in progress | `ota_update.cpp` |

The pattern comes from one pure function (`src/led_pattern.h`). The connected
colors still follow the manual light, rule light, alert, idle order of
`src/alert_state.h`, and the two button patterns sit on top of every state.

Tradeoff: it sits right next to the camera lens, so during the physical
bring-up in `docs/BRINGUP.md`, verify the alert LED does not wash out the
camera's own exposure when both fire close together (a confirmed detection
triggers the alert on the same frame that keeps capturing).

## PSRAM

`camera_config_t.fb_location` is set to `CAMERA_FB_IN_PSRAM` in
`src/main.cpp`. The AI-Thinker ESP32-CAM ships with PSRAM, and stable JPEG
capture at `FRAMESIZE_VGA` or above requires the frame buffer to live in
PSRAM rather than the ESP32's limited internal SRAM, this is the standard
AI-Thinker ESP32-CAM setup used across the Espressif esp32-camera examples.
