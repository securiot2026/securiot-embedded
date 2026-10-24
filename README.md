# securiot-embedded

Firmware for the SecurIoT ESP32-CAM (AI-Thinker) device. Captures frames,
sends them to the Edge API's `POST /frames` endpoint, and acts on the
response: pans/tilts the camera to track a detected person, actuates a
door-lock servo, and fires a local buzzer/status-LED alert.

For a camera-only demo with the local Mac console, use the opt-in MJPEG
preview mode. Start with [the preview quick start](docs/CAMERA_PREVIEW.md);
it does not initialize servos or send frames to the Edge API by itself.

## What this device does

1. Captures a JPEG frame from the camera.
2. Uploads it via `multipart/form-data` to `POST {EDGE_API_URL}/frames`,
   with the `X-Device-Key` header and `device_id`/`zone_id` fields.
3. Parses the JSON response (`pan_delta`, `tilt_delta`, `door_action`,
   `alert`) and drives the pan/tilt servos, the door-lock servo, and the
   local buzzer/status LED accordingly.
4. Reconnects to WiFi automatically if the connection drops, without a
   manual power-cycle.
5. Drives a single WS2812 status LED (GPIO4, see `docs/PINOUT.md`): blue
   while connecting to WiFi, green when idle/connected, red on a confirmed
   detection, and red blinking forever if the camera fails to initialize.

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

## Local camera preview

For camera bring-up without the Edge API, set `CAMERA_PREVIEW_ONLY` to `1`
in the ignored `src/secrets.h`, rebuild, and upload. This mode skips servo
initialization and Edge uploads. The serial log prints the local viewer URL
after Wi-Fi connects. Open that URL from a device on the same network;
`/capture` returns a JPEG; the viewer uses continuous MJPEG video from
`http://<device-ip>:81/stream` instead of polling individual snapshots.
Preview mode uses two PSRAM framebuffers with latest-frame capture and
disables Wi-Fi power saving to reduce latency. Actual FPS depends on the
scene, lighting, JPEG size, and Wi-Fi quality. Use one video viewer at a time;
the stream pauses when its browser tab is hidden. This mode consumes more
power than periodic snapshots.

| Endpoint | Purpose |
| --- | --- |
| `http://<device-ip>/` | Viewer with pause/resume and live FPS |
| `http://<device-ip>/capture` | One JPEG snapshot |
| `http://<device-ip>/status` | Stream state, cumulative sent frames, current FPS |
| `http://<device-ip>:81/stream` | Single-viewer continuous MJPEG |

The [Edge local console](https://github.com/securiot2026/securiot-edge-api/blob/develop/docs/LOCAL_CONSOLE.md)
can pull `/capture` and analyze frames while the camera stays in this mode.
That is a read-only camera bridge, not the normal actuator-control workflow.

The preview has no authentication or TLS. Use it only on a trusted private
network and never forward its port to the Internet. Set the flag back to
`0` to restore normal Edge operation, with the original Edge credentials.

## Documentation

- [`docs/CAMERA_PREVIEW.md`](docs/CAMERA_PREVIEW.md) - camera-only setup,
  endpoints, measured FPS, Edge integration, and troubleshooting.
- [`docs/PINOUT.md`](docs/PINOUT.md) - full GPIO table, camera-reserved
  pins vs. pins used by this project, and why.
- [`docs/WIRING.md`](docs/WIRING.md) - visual connection diagram: every
  external component (servos, buzzer, power supply, FTDI programmer), what
  GPIO/pin it wires to, and the wiring order.
- [`docs/BRINGUP.md`](docs/BRINGUP.md) - manual checklist for flashing,
  configuring, and physically verifying the device.

## Verification status

Camera preview was built, flashed, and tested on a physical ESP32-D0WD
revision 1.0 with 4 MB flash on macOS. At 640 × 480, a 10.30-second stream
sample recorded 214 sent frames, approximately **20.8 FPS**. This is a
measured demo result, not a guaranteed frame rate. A concurrent JPEG
snapshot was also decoded successfully.

The full actuator path is a separate verification task. Successful preview
does not prove servo wiring, door-lock behavior, buzzer operation, cloud
relay, or face-recognition accuracy. Follow [`docs/BRINGUP.md`](docs/BRINGUP.md)
before enabling normal operation.

## Project layout

```
platformio.ini          PlatformIO project config (board, libraries)
src/main.cpp             setup()/loop(), camera init, capture/POST/parse
src/camera_pins.h         AI-Thinker ESP32-CAM camera GPIO mapping
src/camera_preview.{h,cpp} Opt-in local viewer, MJPEG, snapshots, and FPS
src/wifi_manager.{h,cpp}  WiFi connect + reconnect-with-backoff
src/servo_control.{h,cpp} Pan/tilt/door-lock servo control
src/local_alert.{h,cpp}   Buzzer + WS2812 status LED (blue/green/red states)
src/edge_discovery.{h,cpp} mDNS lookup of the Edge API, with static fallback
src/secrets.h.example     Template for WiFi/API credentials (copy to secrets.h)
docs/PINOUT.md            GPIO reference
docs/WIRING.md            Visual connection diagram, every component and pin
docs/BRINGUP.md           Manual flashing/configuration/verification checklist
docs/CAMERA_PREVIEW.md    Camera-only demo and local Edge console integration
```
