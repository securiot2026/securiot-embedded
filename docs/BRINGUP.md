# Manual Bring-Up Checklist

This firmware was written and code-reviewed in an environment with no
PlatformIO/arduino-cli toolchain and no physical ESP32-CAM attached. Every
step below is a manual step for Juan, on his own machine, with the real
hardware. Nothing in this checklist was performed by the executor.

Follow the steps in order, do not skip step 1.

## 1. Power sequencing (do this before anything else)

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

1. Copy `src/secrets.h.example` to `src/secrets.h`.
2. Fill in the real WiFi SSID and password. The ESP32-CAM and the machine
   running the Edge API must be on the same WiFi network.
3. Edge API address, pick one:
   - **mDNS (default, recommended)**: leave `EDGE_API_MDNS_HOST` set to
     `"edge-api"` and set the Edge API host's mDNS hostname to `edge-api`,
     see `securiot-edge-api/README.md`, "LAN discovery (mDNS)". The
     firmware finds the machine automatically at boot, no IP to maintain.
   - **Static IP**: comment out `#define EDGE_API_MDNS_HOST` and set
     `EDGE_API_URL` to the Edge API's LAN IP and port (for example
     `http://192.168.1.50:5000`). Use this if mDNS is not available (see
     the Windows note in the Edge API README) or the network blocks it.
     `EDGE_API_URL` also works as an automatic fallback if mDNS is enabled
     but fails to resolve, so it is worth filling in either way.
4. Set `DEVICE_KEY` to the shared secret configured on the Edge API for
   this device, and set `DEVICE_ID`/`ZONE_ID` to match how this device is
   registered.
5. Rebuild and reflash: `pio run -t upload`.

`src/secrets.h` is listed in `.gitignore` and must never be committed to
this public repository.

## 4. Physical verification checklist

Perform these checks with the device on the external 5V supply, not on USB
power:

- [ ] Open the Serial Monitor (`pio device monitor`, 115200 baud) and
      confirm the log shows a successful WiFi connect with an IP address.
- [ ] Confirm the log shows either "mDNS: resolved Edge API at http://..."
      or, if using the static fallback, "Edge API: using static
      EDGE_API_URL=...". If it instead shows "mDNS: query timed out",
      double check the Edge API host's mDNS hostname (see its README).
- [ ] Confirm the log shows at least one full capture -> POST -> response
      cycle, with `pan_delta`/`tilt_delta`/`door_action`/`alert` printed.
- [ ] Present a person to the camera and confirm the pan and tilt servos
      visibly move to track them.
- [ ] Confirm the door-lock servo actuates (sweeps to its locked position)
      on a confirmed detection.
- [ ] Confirm the buzzer sounds and the alert LED (GPIO4, the onboard
      flash LED, see `docs/PINOUT.md`) lights up on a confirmed detection.
- [ ] With the device running, turn the WiFi router's radio off, wait about
      30 seconds, then turn it back on. Confirm the device resumes sending
      frames automatically, without a manual power-cycle of the ESP32-CAM.
      This is the WiFi-recovery check from `.planning/research/PITFALLS.md`
      Pitfall 2.

If any check fails, capture the Serial Monitor log at the time of failure,
it is the most useful diagnostic for this firmware.
