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
   manual power-cycle. If the WiFi stays down for about 2 minutes it opens the
   setup portal instead, see "If the WiFi disappears" below.
5. Shows its current step on a single WS2812 status LED (GPIO4), see "Status
   LED and reset button" below.
6. Lets you wipe the config and re-pair the device with a push button on GPIO12
   held for 3 s, see "Reset and manual pairing mode" below.

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

## Bringing up a new Edge machine

Use this when the Edge computer changes (new laptop, mini PC, new site) or has to be rebuilt. The board only talks to one Edge address, which it stores in flash, so moving the Edge always ends with re-pairing the board. The full Edge setup lives in the Edge repo, `securiot-edge-api/docs/LOCAL_DEPLOYMENT.md`, this section is the order of steps and the parts that touch the firmware and the cloud.

1. **Prepare the machine.** Same LAN as the board, Python 3.12, and a fixed address (a DHCP reservation in the router). Clone `securiot-edge-api`, branch `develop`, create the virtual environment and install `requirements.txt` plus `onnxruntime`.
2. **Download the models.** They are not in git. `LOCAL_DEPLOYMENT.md` lists the three files (`yolov8n.pt`, `yolov8n-face.pt`, `w600k_r50.onnx`), where to get them and the licence note for the ArcFace weights. Put them in `models/`.
3. **Set the secrets on the Edge.** Four values must match the cloud, and none of them goes in git or in a chat:

   | Edge setting | Must equal |
   | --- | --- |
   | `DEVICE_SHARED_SECRET` | Cloud API `EDGE_DEVICE_SHARED_SECRET` (the Setup QR hands it to the board as the device key) |
   | `EDGE_ADMIN_API_KEY` | Cloud API `EDGE_ADMIN_API_KEY` (live feed, face enrollment and commands) |
   | `CLOUD_API_URL` | The Cloud API base URL. Without it heartbeats go to `localhost:5000`, which is AirPlay on macOS and answers 403 |
   | `CLOUD_DEVICE_API_KEY` | A fresh key from `POST /api/v1/devices/<id>/rotate-key` (owner JWT). The old key stops working at once |

   Also set `DETECTION_BACKEND=yolo`, `FACE_RECOGNITION_ENABLED=true` and the model paths, as in `deploy/macos/edge.env.example`. Keep the file outside the repository.
4. **Start it and check it alone.** Run the Edge on port 5001 (or install the launchd agent from `deploy/macos/`). `curl -i -X POST http://<edge-ip>:5001/frames` without a key must answer 401, which proves the server is up and checking keys.
5. **Let the cloud reach it.** The Cloud API runs on a server that cannot see a private LAN address, so the live feed and face enrollment need a public address for the Edge, for example a Cloudflare tunnel (`LOCAL_DEPLOYMENT.md`, section 4). Save it as the device Edge URL in the Web App, or with `PATCH /api/v1/devices/<id>` and `{ "edgeUrl": "https://..." }`. If the zone uses Bot Fight Mode, allow the server address in the firewall rules, otherwise the Cloud API gets a 403.
6. **Re-pair the board with the new Edge address.** There is no remote way to change it.
   1. Hold the push button on GPIO12 for 3 seconds. The LED blinks magenta, then white, and the board restarts into the setup portal.
   2. Join `SecurIoT-Setup-XXXX` from a phone, pick the WiFi and type its password yourself.
   3. At `http://4.3.2.1/provision-extra`, scan the Setup QR from the Web App (Devices, Setup) or fill the four fields by hand: Edge URL, device key, device id and zone id.
   4. Which Edge URL to enter: the board and the Edge share a LAN, so the LAN address `http://<edge-ip>:5001` is the fast path with no internet in between. The QR carries the device Edge URL from step 5 instead. Use the tunnel address only if you accept the frames going out and back through the internet. Type the LAN address by hand if you want the first option.
   5. Save. The board checks the Edge, stores the config and restarts.
7. **Verify end to end.**
   - Remote log: `nc <board-ip> 23`. It should print `pan_delta=... tilt_delta=...` lines (and `faces count=N known=M` when face recognition is on). Repeated `Camera capture failed` means a camera or power problem, see `docs/DEVELOPMENT.md`.
   - Edge log: `POST /frames` with 200 about every half second to a second. A 401 means the device key differs.
   - Edge log: no `heartbeat: cloud API returned 403` lines. If it appears, fix `CLOUD_API_URL` or rotate the key again.
   - Web App: the device shows online within about a minute and the live feed shows the latest frame.
8. **Optional, face recognition.** Register each person from the Web App (at least 3 photos, with written consent, Peruvian law treats faces as sensitive data). The Edge log then shows `known=1` for them.

Expect about 2 to 3 frames per second from a laptop, since each frame goes through YOLO, face detection and ArcFace on the CPU. The capture pace can be changed with `CAPTURE_INTERVAL_MS` (see `docs/DEVELOPMENT.md`). The capacity of a small mini PC has not been measured yet.

## First setup and pairing

A fresh board has no WiFi and no Edge API address, so it opens its own setup
network instead of guessing. The flow is the same every time:

1. **Boot.** The firmware reads the saved config. With no complete config it
   opens the portal right away. With a saved config it tries that WiFi for up to
   3 x 20 s (about 60 s of slow blue blink) before it gives up and opens the
   portal.
2. **Join the setup network.** From a phone, join `SecurIoT-Setup-XXXX` (the
   last four characters come from the board's MAC). Most phones open the setup
   page by themselves. If yours does not, browse to `http://4.3.2.1`.
3. **Pick the WiFi.** Choose your network and enter its password. The board
   tests the credentials before it accepts them. Do not use the portal's `AP mode` option, the
   firmware rejects it and restarts the portal.
4. **Fill the second form.** At `http://4.3.2.1/provision-extra`, enter the Edge
   API URL, the device key, the Device id and the Zone id. The Device id and
   Zone id must match the Device registered in the Web App, otherwise rules,
   manual control and the live feed will not work. The easy way is the Setup QR
   from the Web App, which prefills all four fields, see
   [`docs/BRINGUP.md`](docs/BRINGUP.md) step 3a.
5. **Save.** The board pings the Edge API, stores the config in flash and
   restarts. After the restart it joins your WiFi and starts sending frames.

Nothing in this flow prints the WiFi password, the device key or the Edge URL
to the serial log.

### Reset and manual pairing mode

Hold the push button on GPIO12 for 3 seconds, at any moment, and the board
erases its saved config (WiFi, Edge API URL, device key, Device id, Zone id) and
restarts straight into the portal. This is the manual pairing mode. It also
works if you hold the button while you power the board on, and during the 60 s
connect phase, so you never have to wait for the portal.

A short press does nothing except print a log line, so a bump will not wipe the
device.

### If the WiFi disappears

If the WiFi stays down for about 2 minutes while the device is running, the
firmware opens the setup portal and capture pauses. Two things to know:

- The portal does not reconnect the old WiFi on its own. The device stays there
  until you finish setup (it then restarts), reset it with the button, or
  power-cycle it.
- The setup network is open, with no password. Do setups in a private place and
  do not leave a device sitting in the portal unattended.

## Status LED and reset button

One WS2812 LED on GPIO4 tells you which step the device is in. All patterns are
non-blocking, so they keep moving while the board works.

| LED | Meaning | What to do |
| --- | --- | --- |
| Slow blue blink | Connecting with saved credentials | Wait, or hold the button 3 s to go straight to the portal |
| Pulsing cyan | Portal open, nobody joined yet | Join `SecurIoT-Setup-XXXX` |
| Solid cyan | A phone joined the setup network | Open `http://4.3.2.1/provision-extra` |
| Pulsing yellow | WiFi accepted, waiting for the second form | Fill the form and save |
| Green | Online and idle | Nothing |
| Red | Confirmed detection | It clears by itself |
| No capture, portal still opens | Camera init failed | The board keeps running without capture and retries every 30 s. The log shows `Camera init failed`. Check the camera ribbon and the 5 V supply |
| Magenta blink that speeds up | Button is being held | Keep holding until it turns white |
| Fast white blink | Reset confirmed | Release the button and wait for the restart |
| Solid orange | Firmware update over WiFi in progress | Do not cut the power, the board restarts by itself and turns green (see `docs/BRINGUP.md` section 3e) |

Manual light commands and rule effects from the Web App still take priority
over the idle colors, as before.

## Wiring at a glance

The full diagram, power sequencing and wiring order are in
[`docs/WIRING.md`](docs/WIRING.md) and [`docs/PINOUT.md`](docs/PINOUT.md). The
short version for the parts that matter here:

| Part | Connect to | Notes |
| --- | --- | --- |
| Reset button | GPIO12 and GND | No resistor needed, the firmware turns on the internal pull-up. GPIO12 is a boot pin, but a button to GND holds it LOW, which is the safe level, so pressing it while powering on is fine |
| WS2812 DIN | GPIO4 through a 330 to 470 ohm resistor | Use DIN, not DOUT. The resistor calms the 3.3 V data line |
| WS2812 VCC | 5 V from the external supply | Check your module's silkscreen |
| WS2812 GND | The shared ground | Every component shares one ground |
| Buzzer | GPIO2 and GND | |
| Pan, tilt, door servos | GPIO13, GPIO15, GPIO14 | Powered from the external 5 V, not from the FTDI |

Do not use GPIO0, GPIO16 or any camera pin for extras: GPIO0 is the camera
clock and the flash-mode pin, and GPIO16 is used by the PSRAM. Do not insert a
microSD card either, it shares GPIO12.

## Checking the new behavior on hardware

The button, LED patterns and WiFi-loss portal are covered by host tests
(`pio test -e native`), but they have not been run on a physical board yet.
Before you rely on them:

1. Wire the button and the resistor, boot with the button released, and look
   for `Button: GPIO12 ready` in the serial log.
2. Boot again with the button held. The board must still start, show the magenta
   countdown, then the fast white blink after 3 s, and open the portal.
3. Hold the button for 3 s during the 60 s connect phase, and again with the
   portal open.
4. Walk through the LED sequence: slow blue, pulsing cyan, solid cyan after the
   phone joins, pulsing yellow after the WiFi step, green when online.
5. Turn the router's radio off for more than 2 minutes and look for
   `WiFi: link down for 120 s, reopening portal`. Then provision the board and
   confirm it restarts and sends frames again.

The longer version with expected log lines is in
[`docs/BRINGUP.md`](docs/BRINGUP.md) sections 3c, 3d and 4.

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
- [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) - GitFlow, CI, OTA flashing, LEDC timer rules, capture rate.
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
src/local_alert.{h,cpp}   Buzzer + WS2812 status LED (single paint path for all states)
src/reset_button.{h,cpp}  GPIO12 reset and pairing button (3 s hold erases config)
src/button_state.h        Pure debounce and long press state machine (host-tested)
src/led_pattern.h         Pure state to LED color function (host-tested)
src/edge_discovery.{h,cpp} mDNS lookup of the Edge API, with static fallback
src/secrets.h.example     Template for WiFi/API credentials (copy to secrets.h)
docs/PINOUT.md            GPIO reference
docs/WIRING.md            Visual connection diagram, every component and pin
docs/BRINGUP.md           Manual flashing/configuration/verification checklist
docs/CAMERA_PREVIEW.md    Camera-only demo and local Edge console integration
```
