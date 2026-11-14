# Development workflow for the firmware

This page covers how to change, test, flash and ship the ESP32-CAM firmware.
Hardware wiring is in [`WIRING.md`](WIRING.md), first bring-up in
[`BRINGUP.md`](BRINGUP.md).

## Branches and GitFlow

| Branch | Purpose |
| --- | --- |
| `main` | Released firmware. Only receives merges from `develop` |
| `develop` | Integration branch. Every feature and fix lands here first |
| `feature/<name>`, `fix/<name>`, `feat/<name>`, `docs/<name>`, `chore/<name>` | Short lived work, always branched from `develop` |

1. `git checkout develop && git pull`
2. `git checkout -b fix/short-name`
3. Commit with Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`, `ci:`).
4. Open a pull request into `develop`. Wait for the `firmware` check to pass.
5. Merge, delete the branch, then release by opening a pull request from
   `develop` into `main`.

GitHub does not let this private repository use branch protection on the free
plan, so nobody enforces the CI check for you. Wait for it by hand.

## CI

`.github/workflows/ci.yml` runs on every push and pull request:

1. `pio test -e native` runs the host tests (206 cases at the time of writing).
2. It copies `src/secrets.h.example` to `src/secrets.h`.
3. `pio run -e esp32cam` builds the real firmware.

Run the same two commands locally before you open a pull request.

## Secrets

`src/secrets.h` is ignored by git. Copy `src/secrets.h.example` and fill it in.
Never paste the WiFi password, the device key or `OTA_PASSWORD` into an issue,
a pull request or a chat. The WiFi password is typed in the setup portal and
the device key comes from the Web App Setup QR.

## Flashing

First flash, or any flash after a partition change, goes over USB:

```bash
pio run -e esp32cam -t upload
```

Every later flash can go over WiFi (OTA). The board must be on the LAN and
`OTA_PASSWORD` must match the one built into the running firmware.

```bash
export SECURIOT_OTA_PASSWORD='<the OTA_PASSWORD from secrets.h>'
pio run -e esp32cam_ota -t upload --upload-port <board-ip>
```

Use the board IP instead of `securiot-cam.local`. mDNS often does not resolve
on macOS or on phone hotspots. Find the IP in your router, with
`arp -a | grep -i 58:bf:25`, or from the Edge API log (`POST /frames` lines show
the source address). A wrong password is rejected, which is the expected
behavior. The remote log in [`BRINGUP.md`](BRINGUP.md) section 3e shows the same
messages the serial port prints:

```bash
nc <board-ip> 23
```

The mirror only shows lines printed after you connect, there is no history.

## Camera and servo timers (do not undo)

The camera drives its 20 MHz clock from an LEDC timer. The servo library hands
out LEDC timers and retunes them to 50 Hz. If both use the same timer, the
camera stops and every capture fails with `Camera capture failed, skipping this
cycle`. The firmware keeps them apart on purpose:

- Camera XCLK: LEDC timer 3, channel 7 (`src/main.cpp`).
- Servos: timers 0 to 2 only (`src/servo_control.cpp`).
- Buzzer: whatever channel the Arduino core assigns, from channel 0 upward.

If you add another LEDC user (another servo, a buzzer, a PWM LED), keep it below
channel 6 and out of timer 3.

## Capture rate

`CAPTURE_INTERVAL_MS` in `src/main.cpp` is the minimum gap between uploads, 100
ms by default. Override it with `-DCAPTURE_INTERVAL_MS=...` in `platformio.ini`.
The camera uses two frame buffers and always grabs the latest frame. The real
rate depends on the Edge API: every frame goes through YOLO, face detection and
ArcFace on the CPU. On a laptop that gave about 2.5 frames per second. To go
faster, lower `CAPTURE_FRAME_SIZE` to `FRAMESIZE_QVGA` (less detection accuracy)
or run the Edge API on faster hardware.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Camera capture failed` forever | Loose camera ribbon, weak 5 V supply, or an LEDC timer clash (see above) |
| `Edge API returned HTTP 401` | The device key in the board does not match the Edge `DEVICE_SHARED_SECRET`. Re-provision with the Setup QR |
| No frames reach the Edge after the laptop changed network | The board stores the Edge IP. Re-provision it with the Setup QR, or reserve a fixed IP for the Edge machine |
| OTA says no response | Board offline, wrong IP, or firewall blocking UDP 3232. Ping the board first |
| OTA says authentication failed | `SECURIOT_OTA_PASSWORD` differs from the password in the running firmware |
