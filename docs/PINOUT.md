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
| 12 | MTDI / SD_DATA2 strapping pin | If pulled HIGH at boot the chip selects 1.8V flash I/O instead of 3.3V, which fails to boot this board's 3.3V flash. Never wired to a peripheral that could be high at power-on |
| 16 | PSRAM | Used internally for PSRAM on this module's chip revision, not available as a general-purpose pin |

## Available GPIOs used by this project

| GPIO | Used by | File | Notes |
|---|---|---|---|
| 13 | Pan servo | `src/servo_control.cpp` | Safe general-purpose output, no boot-strapping role |
| 15 | Tilt servo | `src/servo_control.cpp` | Strapping pin for JTAG selection, but safe to drive as an output once boot has completed; not loaded externally during boot in this wiring |
| 14 | Door-lock servo | `src/servo_control.cpp` | Safe general-purpose output |
| 2 | Buzzer (LEDC tone) | `src/local_alert.cpp` | Strapping pin, must be LOW or floating during boot for normal boot mode; a servo motor or buzzer driver on this pin does not present a conflicting pull at power-on in this wiring, but confirm during Juan's physical bring-up (see docs/BRINGUP.md step 4) |
| 4 | Local alert LED | `src/local_alert.cpp` | Reuses the AI-Thinker board's onboard flash LED instead of wiring a separate external LED, see "Onboard flash LED reuse" below |

GPIO 12 is deliberately left unused by this project because of its flash
voltage-selection strapping role documented above, there was no need to
risk it when GPIOs 13/14/15/2/4 already cover all five peripherals.

## Onboard flash LED reuse (GPIO4)

The AI-Thinker ESP32-CAM has a bright white LED soldered next to the camera
on GPIO4, normally used as a photography flash. This project reuses it as
the local-alert LED (`triggerLocalAlert()` in `src/local_alert.cpp`) instead
of wiring a separate external LED, because:

- It needs no extra wiring or GPIO, freeing a pin for other use if the
  project grows.
- It is bright enough to be seen from the same distance a person would be
  detected from, satisfying HW-05's local visual alert requirement.

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
