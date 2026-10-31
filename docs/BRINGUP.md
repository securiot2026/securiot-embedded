# Manual Bring-Up Checklist

This checklist covers the normal Edge/actuator workflow. Camera-only
preview has been verified on real hardware; servo, door, buzzer, and
external-power checks below still require an operator to verify them.
For the camera-only path, see [Camera preview](CAMERA_PREVIEW.md).

Follow the steps in order, do not skip step 1.

## 1. Power sequencing (do this before anything else)

See [`docs/WIRING.md`](WIRING.md) for the full connection diagram covering
every step below, GPIO by GPIO.

The camera plus 3 servos can draw enough current, especially during a WiFi
transmit burst, to brown out a supply that is only backed by a USB-serial
programmer's 5V line. This is a documented risk for this exact hardware
combination (see `.planning/research/PITFALLS.md` and the phase ROADMAP).

1. Wire the ESP32-CAM's 5V pin, the camera module (already onboard), and all
   3 servos to a single external 5V power supply rated for at least 2A,
   not to the FTDI/USB-serial adapter's 5V output.
2. Tie the external supply's ground to the ESP32-CAM's GND and to every
   servo's GND, a shared common ground across all of them is required, a
   floating ground causes erratic servo behavior and resets.
3. Only connect the USB-serial adapter's 5V line to the board during
   flashing (step 2 below), with everything else already on the external
   5V supply, never power camera and servos from USB alone during normal
   operation.

## 2. Flashing

1. Install PlatformIO (VS Code extension) or Arduino IDE 2.x with the
   ESP32 board package, per PlatformIO's or Espressif's own setup
   instructions.
2. Connect an FTDI/USB-serial adapter to the ESP32-CAM: TX to U0R, RX to
   U0T, 5V and GND per the adapter's own wiring, following the standard
   AI-Thinker ESP32-CAM flashing procedure.
3. Bridge GPIO0 to GND before powering on, this puts the board into
   flashing mode. Remove this jumper after flashing, leaving it connected
   during normal boot prevents the firmware from running.
4. From the project root, run:
   ```
   pio run -t upload
   ```
5. After the upload finishes, remove the GPIO0-to-GND jumper, then power
   the board from the external 5V supply set up in step 1, not from the
   USB-serial adapter, for normal operation.

## 3. Configuration

WiFi, Edge API URL, device key, Device id and Zone id are not compiled into
the firmware. The operator enters them in the captive portal and the device
stores them in NVS, so they survive a reboot without reflashing.

1. Copy `src/secrets.h.example` to `src/secrets.h` before building. Keep
   `CAMERA_PREVIEW_ONLY` set to `0` for this normal-operation checklist;
   mode `1` intentionally skips servos and device-to-Edge uploads.
   `DEVICE_ID` and `ZONE_ID` in this file are only fallback defaults, used
   when NVS holds no portal-provided identity.
2. Power on an unconfigured device. It opens the access point
   `SecurIoT-Setup-XXXX` (the last four hex digits of its MAC address).

### 3a. Recommended: Setup QR

Prerequisites:

- The Cloud API has `EDGE_DEVICE_SHARED_SECRET` set to the same value as the
  Edge `DEVICE_SHARED_SECRET`.
- The device's Edge URL is saved on its Face recognition page in the Web App.

Steps:

1. In the Web App open Devices, press "Configure device" on the row of this
   device, then press "Show setup QR".
2. Join `SecurIoT-Setup-XXXX` from the phone. Check that the suffix matches
   the one of your device.
3. Scan the QR with the phone camera and open the link. It points at
   `http://4.3.2.1/provision-extra`, the fixed address of the setup access
   point.
4. If you scan before entering the WiFi, the page first sends you to the WiFi
   form. Type the WiFi network name and password. The next page already has
   Edge API URL, device key, Device id and Zone id filled. Check them and press
   "Save and connect".
5. An invalid or missing value in the link simply leaves that field empty, so
   you can type it yourself. The manual steps below still work for any field.

Secret handling:

- The QR and the link contain the device key. The setup access point is open
  and the link travels unencrypted, so do the setup in a private place.
- Do not share or screenshot the QR. The Web App hides it after about 2
  minutes.
- Open the link only while the phone is connected to the SecurIoT-Setup
  network, because `4.3.2.1` is a routable public address on any other
  network.
- The setup page strips the secret from the address bar after it loads and is
  served with no-store caching.

### 3b. Manual fallback

1. Join the `SecurIoT-Setup-XXXX` network from a phone or laptop and fill in
   the WiFi form (network name and password).
2. After the WiFi credentials are accepted, stay connected to the same
   network and open `http://4.3.2.1/provision-extra`. Most phones open it
   automatically. Fill in four fields:
   - **Edge API URL**: for example `http://192.168.1.50:5000`.
   - **Device key**: the shared secret configured on the Edge API.
   - **Device id**: the Cloud Device UUID of this device in the Web App.
     Open any per-device page in the Web App and read it from the address
     bar, the path looks like `/devices/DEVICE-ID/rules`. You can also
     read the `id` field of `GET /api/v1/devices` (the Cloud API serves
     Swagger UI at `/api/docs`).
   - **Zone id**: the `zoneId` field of that same device in the same
     `GET /api/v1/devices` response.
3. Press "Save and connect". The device checks that the Edge API answers,
   saves the values and restarts. An id with a space, a slash or any
   character other than letters, digits, dot, underscore and hyphen is
   rejected and nothing is saved.

Why the Device id and Zone id must match the Cloud Device: Edge keys rules,
manual commands, the latest frame and occupancy by Device id, and the Cloud
API proxies those calls with the Cloud Device UUID. A different value breaks
rules, manual control and the live feed.

The Edge URL that the Cloud uses to reach the Edge API is saved separately,
on the device's Face recognition page in the Web App.

A device provisioned before this change has no Device id or Zone id in NVS.
It keeps sending the compiled fallback values from `secrets.h` until you
re-provision it. The portal reopens after three failed WiFi attempts, or
after you clear NVS.

`src/secrets.h` is listed in `.gitignore` and must never be committed,
regardless of repository visibility.

### 3c. Reading the portal log

Keep the Serial Monitor open at 115200 baud while the portal is up. The
firmware prints these line families:

- `Provisioning: state A -> B`: every state change of the portal library, by
  name (for example `PORTAL_STARTING -> PORTAL_STARTED`).
- `Portal WiFi: ...`: `AP station connected` and `AP station disconnected`
  (with the last 3 MAC bytes and `ap_clients`), `AP station IP assigned`,
  `STA started`, `STA connected`, `STA got IP x.x.x.x` and
  `STA disconnected reason=N`.
- `Portal HTTP: METHOD /path`: one line per request that reached the portal
  web server. Phone connectivity checks such as `/generate_204`,
  `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt` and `/fwlink` carry
  a `[captive-probe]` tag. The line appears once the request is complete.
- `Provisioning: portal alive, state=..., ap_clients=N, uptime_s=T`: a
  heartbeat every 10 seconds while the portal is open.
- `Provisioning: credential test result=success wifi_status=3 (WL_CONNECTED)`
  or `credential test result=failure wifi_status=N (...)`: the outcome of the
  WiFi test after you submit the WiFi form. `Provisioning: WiFi status A -> B`
  lines show each change of the WiFi status on the way.
- `Provisioning: Edge API ping code=N elapsed_ms=T`: the reachability check of
  the Edge API URL after you submit the setup form. N is the HTTP status, or a
  negative HTTPClient error such as `-1` (connect failed or timed out) or `-11`
  (read timeout). T is the time the check took and stays under about 3500 ms. A
  failed check shows the retry page and saves nothing.
- `WiFi: power save disabled`: printed once after every WiFi link-up (boot
  connect, each reconnect, the portal STA). If it is missing after a connect,
  the radio is still in modem sleep, which causes high ping and lost packets.
  `WiFi: power save disable failed err=N` means the driver refused the change.
- `HTTP POST failed: code=N`: the frame upload to the Edge API failed. N is the
  numeric HTTPClient code: `-1` the connect failed or timed out, `-2` send
  header failed, `-3` send payload failed, `-4` not connected, `-5` connection
  lost, `-11` the Edge did not answer in time. Codes above 0 print as
  `Edge API returned HTTP N`.
- `Provisioning: no complete config, cleared stale WiFi settings, starting
  portal`: at boot the device found no complete saved config, erased the
  leftover WiFi settings of the portal library and opened the portal, so enter
  the WiFi again.
- `Provisioning: PORTAL_COMPLETE rejected reason=ap_mode, restarting portal`
  (also `reason=no_ssid`): the `AP mode` option of the WiFi page is not
  supported, leave it unchecked. The portal restarts and the phone must rejoin
  `SecurIoT-Setup-XXXX`.

Where it is stuck:

- The phone joined but there is no `AP station IP assigned` line: the phone
  never got an address from DHCP.
- `AP station IP assigned` but no `Portal HTTP:` lines: the phone never reached
  port 80. DNS queries and captive-portal probing are not logged here, and a
  request that never completes is not logged either.
- `ap_clients=0` while the phone says it is connected: the phone already left
  the setup network.
- `credential test result=failure` with `wifi_status` 4 or 5, plus
  `STA disconnected reason=15` or `reason=202`: usually a wrong WiFi password.
- `STA disconnected reason=201` with `wifi_status` 1: the network was not
  found (wrong name, out of range, or a 5 GHz only network).
- The log shows the WiFi connected and then nothing, no portal and no further
  lines: the portal library's own saved WiFi settings and the device config
  disagree. Current firmware clears this at boot. If the board is silent after
  WiFi connected, erase NVS with
  `esptool.py --chip esp32 --port <serial port> erase_region 0x9000 0x5000`
  (board in flashing mode as in section 2) or reopen the portal.

The log never prints the network name, the password, the device key, the Edge
URL, query strings or form data. It only shows the last 3 bytes of a MAC
address and the local STA IP.

#### Network stability notes

- WiFi power save is switched off after every link-up. The default modem sleep
  of the Arduino-ESP32 core delayed packets on the real board (mean ping 344 ms
  with loss).
- The `/frames` POST waits at most 1500 ms for the connection and 2500 ms for
  the response, about 4 s in total.
- The upload of the JPEG body itself is not bounded by these two values: the
  core retries a stalled send up to 10 times at 1 s each, so a very bad link can
  still stall one cycle longer.
- If the Edge inference regularly takes longer than 2.5 s, the log shows
  `code=-11`. Raise `LINK_FRAMES_POST_RESPONSE_TIMEOUT_MS` in
  `src/link_stability.h` (keep connect plus response within
  `LINK_FRAMES_POST_BUDGET_MS`).

#### Servo motion profile

- Pan and tilt move on a non-blocking 50 Hz profile run by `servoTick()` at the
  top of `loop()`. `applyPanTilt()` only sets the target:
  `target = clamp(target + delta*10, 0, 180)`. The latest command wins.
- Constants (`src/servo_motion.h`): tick 20 ms, gain k 8 1/s, a_max 400 deg/s
  squared, v_max 90 deg/s, anti-hunting 0.25 deg and 2 deg/s, 10 deg per
  full-scale delta.
- The first proposal for a_max was 360. It was raised to 400 because 360 sits
  exactly on the stability boundary k*v_max/2 and overshot 0.83 deg on a 134 deg
  step. At 400 every step from 1 to 180 deg overshoots 0.5 deg or less.
- A 10 deg step settles in about 0.5 s. `Servo.write()` is called only when the
  integer degree changes, and never when the target equals the angle.
- The boot messages `ledcAttachChannel(): Pin 13/15/14 is already attached to
  LEDC` and `ESP32PWM ERROR PWM channel failed to configure` come from
  ESP32Servo 3.2.1 attaching each pin twice internally. They are harmless and
  the servos still move.

#### Door open and close

- `{actuator: door, action: {state: open|close}}` moves the door servo: open is
  0 deg, close is 180 deg. Only the exact lowercase strings are accepted; any
  other state is ignored.
- The motion reuses the pan/tilt ramp (about 2.3 s for a full swing, measured
  at 114 ticks). Repeating the same command writes nothing.
- The door servo is not written at boot. A servo holds its last position and
  the firmware cannot know it, so the first door command after a reboot writes
  its own angle directly (one write) and every later command ramps.
- The legacy `door_lock` manual command and the Edge rule `door_action: lock`
  stay as an alias of close.

## 4. Physical verification checklist

Perform these checks with the device on the external 5V supply, not on USB
power:

- [ ] With the board running, a pan or tilt command glides instead of snapping,
      and a new command mid-motion redirects smoothly.
- [ ] Door: from the Mobile App send open and then close. Confirm the horn
      moves smoothly (about 2 s) and that rebooting the board does not move it.
- [ ] Power on and confirm the status LED shows blue (WiFi connecting),
      then turns green once connected. If it instead blinks red forever,
      camera init failed, see the Serial Monitor for the error.
- [ ] Open the Serial Monitor (`pio device monitor`, 115200 baud) and
      confirm the log shows a successful WiFi connect with an IP address.
- [ ] Confirm the boot log shows the line `Identity: device_id=... zone_id=...`
      and that both values match the Device id and Zone id in the Web App.
- [ ] After provisioning, open the Web App live-feed page for that device
      (`/devices/DEVICE-ID/live-feed`) and confirm it shows a frame. This
      proves Edge keyed the frame by the Cloud Device UUID.
- [ ] In the portal, enter a Device id containing a space or a slash and
      confirm the form is rejected and nothing is saved (the device does not
      restart).
- [ ] The Setup QR flow prefills all four fields (Edge API URL, device key,
      Device id and Zone id) on the page that follows the WiFi step.
- [ ] Open a setup link with a bad Device id (for example one containing a
      slash) and confirm that field is left empty while the other three are
      still filled.
- [ ] While the portal is open, the Serial log shows at most about two
      `nvs_open failed` lines per second instead of a flood.
- [ ] With a phone joined to `SecurIoT-Setup-XXXX`, the Serial log shows
      `AP station connected`, then `AP station IP assigned`, then
      `Portal HTTP:` lines including at least one `[captive-probe]`.
- [ ] Submit `/provision-extra` with an Edge API URL that does not answer and
      confirm the retry page appears within about 3.5 seconds, the log shows
      `Edge API ping code=` with `elapsed_ms` under 3500 and the board does not
      reboot.
- [ ] After each WiFi connect the log shows `WiFi: power save disabled`
      exactly once.
- [ ] With the board running, a ping from a laptop on the same network to the
      board shows steady replies without multi-hundred-millisecond swings and
      without loss, and `HTTP POST failed: code=` lines are rare or absent.
- [ ] Tick `AP mode` on the WiFi page and confirm the log shows
      `PORTAL_COMPLETE rejected reason=ap_mode` and the SecurIoT-Setup portal
      comes back.
- [ ] While the portal stays open, a `portal alive` line appears about every
      10 seconds and contains no secret.
- [ ] Confirm the log shows at least one full capture -> POST -> response
      cycle, with `pan_delta`/`tilt_delta`/`door_action`/`alert` printed.
- [ ] Present a person to the camera and confirm the pan and tilt servos
      visibly move to track them.
- [ ] Confirm the door-lock servo actuates (sweeps to its locked position)
      on a confirmed detection.
- [ ] Confirm the buzzer sounds and the status LED (GPIO4, external WS2812,
      see `docs/PINOUT.md`) switches to red on a confirmed detection, then
      back to green once the detection clears.
- [ ] With the device running, turn the WiFi router's radio off, wait about
      30 seconds, then turn it back on. Confirm the device resumes sending
      frames automatically, without a manual power-cycle of the ESP32-CAM.
      This is the WiFi-recovery check from `.planning/research/PITFALLS.md`
      Pitfall 2.
- [ ] Rules (phase 12): in the Web App, create a rule whose action is a
      light with a color (for example blue). When it fires, confirm the
      status LED shows that color for about 5 s and then returns to green.
- [ ] Create a rule whose action is the buzzer. When it fires, confirm the
      buzzer sounds for about 5 s and then stops on its own.
- [ ] From the Mobile app turn Light or Buzzer on. Confirm it stays on across
      frames (the Serial log keeps printing `alert=false`) until you toggle it
      off, or for about 2 minutes if you do not. A person detection must still
      give the red LED and the beep, and then return to the held state.
- [ ] With face recognition enabled on the Edge, confirm the Serial log prints
      a `faces count=N known=M` line. No identity name must appear in the log.
- [ ] A manual `door_lock` command is only reachable through a dry-run with
      execute true. Run one and confirm the door-lock servo sweeps to its
      locked position.

If any check fails, capture the Serial Monitor log at the time of failure,
it is the most useful diagnostic for this firmware.
