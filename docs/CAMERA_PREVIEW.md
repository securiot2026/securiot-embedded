# Camera preview and local Edge integration

Use this mode to view the physical ESP32-CAM and run recognition on a Mac
without initializing servos or changing the door state. Only use it on a
trusted private network: the camera's HTTP endpoints have no authentication
or TLS.

## Quick start

1. Install PlatformIO Core and connect the AI-Thinker ESP32-CAM through its
   USB-to-serial adapter. Follow [power and wiring guidance](BRINGUP.md)
   before connecting external actuators.
2. Create the private configuration:

   ```bash
   cp src/secrets.h.example src/secrets.h
   ```

   Set your own Wi-Fi SSID/password and `CAMERA_PREVIEW_ONLY` to `1` in
   `src/secrets.h`. Do not put actual credentials in the example file.
3. Identify the serial port, then build and upload:

   ```bash
   pio device list
   pio run -e esp32cam
   pio run -e esp32cam -t upload --upload-port /dev/cu.usbserial-110
   pio device monitor --port /dev/cu.usbserial-110 --baud 115200
   ```

   The port shown above is an example from the tested Mac, not a fixed
   device identifier. Close any other serial monitor before uploading.
4. Wait for `WiFi: connected, IP=...` and `Camera preview: http://.../`.
   Open that URL on a device connected to the same Wi-Fi network.

The project pins pioarduino `53.03.13` / Arduino-ESP32 `3.1.3`; it uses
the core 3.x LEDC API. Replacing this with an older default platform is not
an equivalent setup.

## Choose the correct operating mode

| Behavior | `CAMERA_PREVIEW_ONLY=1` | `CAMERA_PREVIEW_ONLY=0` |
| --- | --- | --- |
| Local viewer and MJPEG | Enabled | Not started |
| Camera buffering | Two PSRAM buffers, latest-frame capture | Single buffer |
| Wi-Fi power saving | Disabled for streaming | Existing normal behavior |
| Servo initialization | Skipped | Enabled |
| Device POSTs to Edge `/frames` | Skipped | Enabled |
| Applies returned servo/door commands | No | Yes |

The Edge can still analyze the camera in preview mode by pulling snapshots
from the Mac. This does **not** make actuator commands run on the device.
To restore normal operation, set the flag to `0`, configure the real Edge
URL/device key, rebuild, and upload. Verify the actuator checklist separately.

## HTTP endpoints

Replace `<device-ip>` with the address printed by the serial log. DHCP can
assign a different address after reconnecting the hotspot.

| URL | Response |
| --- | --- |
| `http://<device-ip>/` | Viewer; pauses when hidden and supports manual pause/resume |
| `http://<device-ip>/capture` | A JPEG frame, with caching disabled |
| `http://<device-ip>/status` | JSON stream telemetry |
| `http://<device-ip>:81/stream` | `multipart/x-mixed-replace` MJPEG video |

Example telemetry, with illustrative values:

```json
{"streaming": true, "frames": 214, "fps": 20.8}
```

`frames` counts successful stream-frame sends since boot; `fps` describes
the current stream's recent send rate. It is not a display-rendering rate,
an inference rate, or a recognition-accuracy metric. Snapshot requests are
not counted in that stream counter.

Use one MJPEG viewer at a time. The stream handler is long-lived, so a
second stream connection may wait or time out. The separate port-80
server keeps snapshots and telemetry available while video is active.

## Connect the Mac Edge console

In [securiot-edge-api on `develop`](https://github.com/securiot2026/securiot-edge-api/tree/develop),
complete the [local console setup](https://github.com/securiot2026/securiot-edge-api/blob/develop/docs/LOCAL_CONSOLE.md),
then run:

```bash
python scripts/run_local_console.py --camera-url http://<device-ip> --port 5050
```

Open `http://127.0.0.1:5050/` on that Mac. The console bridge pulls
`/capture` and submits JPEGs to the authenticated local Edge `/frames`
endpoint. Video, inference, and face enrollment are separate operations;
high video FPS does not imply the same number of recognitions per second.

## Measured bring-up result

On the tested ESP32-D0WD revision 1.0 with 4 MB flash:

- A 640 × 480 stream sent 214 frames over 10.30 seconds: **20.78 FPS**.
- Recent-rate telemetry samples ranged from **18.93 to 22.13 FPS**.
- A concurrent `/capture` request returned a fully decoded 640 × 480 JPEG.
- Preview and normal-mode builds succeeded; physical actuator operation
  was not verified by these camera tests.

The previous snapshot viewer waited 500 ms between images, capping it
below 2 FPS. Continuous MJPEG removes that polling delay. Results still
depend on lighting, exposure, JPEG size, Wi-Fi quality, and power stability;
20.78 FPS is not a guaranteed minimum.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Wi-Fi retries, no IP | Exact SSID/password, hotspot availability, and 2.4 GHz support. On supported iPhones, enable Allow Others to Join and Maximize Compatibility; keep that settings screen open while joining. |
| Camera reachable from phone but not Mac | Join the Mac to the same hotspot. Do not assume an old DHCP address is still valid. |
| Second video tab hangs | Close or pause the first MJPEG viewer. Use `/status` for passive FPS measurement. |
| Upload cannot open serial port | Close `screen`, PlatformIO monitors, and other serial clients first. |
| Corrupt serial transfers | Lower baud rate. The tested adapter failed a full-flash read at 460800 but succeeded at 230400; normal uploads used 115200. |
| `Camera init failed` | Check board model, camera ribbon, PSRAM, wiring, and stable power. |
| Servo/PWM errors in normal mode | Preview intentionally skips servos; it is not a fix or validation of the normal servo path. |

## Before sharing or publishing

- Never commit `src/secrets.h`, firmware flash backups, or private Wi-Fi keys.
- Compiled binaries and flash dumps can contain credentials even when the
  source configuration is ignored. Keep those artifacts private too.
- Do not port-forward the preview endpoints or use them on public Wi-Fi.
