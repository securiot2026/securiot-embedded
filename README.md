# securiot-embedded

Firmware for the SecurIoT ESP32-CAM (AI-Thinker) device. Captures frames,
sends them to the Edge API's `POST /frames` endpoint, and acts on the
response: pans/tilts the camera to track a detected person, actuates a
door-lock servo, and fires a local buzzer/LED alert.

## What this device does

1. Captures a JPEG frame from the camera.
2. Uploads it via `multipart/form-data` to `POST {EDGE_API_URL}/frames`,
   with the `X-Device-Key` header and `device_id`/`zone_id` fields.
3. Parses the JSON response (`pan_delta`, `tilt_delta`, `door_action`,
   `alert`) and drives the pan/tilt servos, the door-lock servo, and the
   local buzzer/LED accordingly.
4. Reconnects to WiFi automatically if the connection drops, without a
   manual power-cycle.

The exact request/response contract is documented in the Edge API's own
README (`repos/securiot-edge-api/README.md`, "Camera detection contract").

## Finding the Edge API on the network

The Edge API is meant to run on a machine local to the installation site
(the ESP32-CAM's own WiFi network), not on the Cloud API's VPS, YOLO
inference is too heavy for a small VPS and the device needs LAN-level
latency anyway. See `repos/securiot-edge-api/README.md`, "Where this
runs", for the full picture.

This firmware finds that machine automatically via mDNS, no IP to
hardcode or update:

1. At boot, and again after 3 consecutive failed requests (the previously
   resolved IP likely changed), it queries `EDGE_API_MDNS_HOST` (`secrets.h`,
   default `"edge-api"`, resolving `edge-api.local`).
2. If that succeeds, it uses the returned IP with `EDGE_API_PORT`.
3. If it fails (no mDNS responder reachable, or `EDGE_API_MDNS_HOST` is
   commented out), it falls back to the static `EDGE_API_URL`.

Setup on the Edge API side (setting the machine's mDNS hostname) is in
`repos/securiot-edge-api/README.md`, "LAN discovery (mDNS)". Configuring
this firmware's side of it is in `docs/BRINGUP.md`, step 3.

## Documentation

- [`docs/PINOUT.md`](docs/PINOUT.md) - full GPIO table, camera-reserved
  pins vs. pins used by this project, and why.
- [`docs/WIRING.md`](docs/WIRING.md) - visual connection diagram: every
  external component (servos, buzzer, power supply, FTDI programmer), what
  GPIO/pin it wires to, and the wiring order.
- [`docs/BRINGUP.md`](docs/BRINGUP.md) - manual checklist for flashing,
  configuring, and physically verifying the device.

## Development environment note

This firmware was written and code-reviewed without a physical ESP32-CAM
or a PlatformIO/arduino-cli toolchain available in the development
environment. Every external API call (`esp_camera.h`, `WiFi.h`,
`HTTPClient.h`, `ESP32Servo`, `ArduinoJson`) was verified against its
official documentation. Compiling, flashing, and physical bring-up are
Juan's manual steps, documented in `docs/BRINGUP.md`, this is a deliberate
handoff, not a gap in the implementation.

## Project layout

```
platformio.ini          PlatformIO project config (board, libraries)
src/main.cpp             setup()/loop(), camera init, capture/POST/parse
src/camera_pins.h         AI-Thinker ESP32-CAM camera GPIO mapping
src/wifi_manager.{h,cpp}  WiFi connect + reconnect-with-backoff
src/servo_control.{h,cpp} Pan/tilt/door-lock servo control
src/local_alert.{h,cpp}   Buzzer/LED local alert
src/edge_discovery.{h,cpp} mDNS lookup of the Edge API, with static fallback
src/secrets.h.example     Template for WiFi/API credentials (copy to secrets.h)
docs/PINOUT.md            GPIO reference
docs/WIRING.md            Visual connection diagram, every component and pin
docs/BRINGUP.md           Manual flashing/configuration/verification checklist
```
