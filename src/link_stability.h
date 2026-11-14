#ifndef LINK_STABILITY_H
#define LINK_STABILITY_H

// Pure, Arduino-free helpers for the WiFi link stability fixes of the main
// firmware. Header-only, standard headers only, so the same code compiles into
// the firmware and into the host-side Unity tests (`pio test -e native`).
//
// The formatters only accept numbers, never a string, so no log line built here
// can carry a URL, a key, a network name or a password.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---- Power save ------------------------------------------------------------
//
// The Arduino-ESP32 core starts WiFi in WIFI_PS_MIN_MODEM (modem sleep), which
// delays and drops packets on the real board. The firmware switches it off after
// every STA link-up. True only on the rising edge (the link was down and is up
// now), so the apply and its log line happen once per link-up and not once per
// loop iteration.
static inline bool linkShouldApplyPowerSave(bool wasConnected, bool isConnected) {
    return isConnected && !wasConnected;
}

// Writes `WiFi: power save disabled` when espErr is 0, otherwise
// `WiFi: power save disable failed err=<espErr>`. Always NUL-terminates when
// outSize is at least 1, writes nothing for a NULL out or outSize 0. Returns the
// snprintf result (0 for the no-op cases).
static inline int linkFormatPowerSaveLog(char *out, size_t outSize, int espErr) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    if (espErr == 0) {
        return snprintf(out, outSize, "WiFi: power save disabled");
    }
    return snprintf(out, outSize, "WiFi: power save disable failed err=%d", espErr);
}

// ---- /frames POST budget ---------------------------------------------------
//
// HTTPClient limits for POST /frames. The response timeout leaves room for the
// Edge YOLO inference. The budget covers the connect and the wait for the
// response header ONLY. The multipart payload write is NOT bounded by these two
// values: NetworkClient::write retries a stalled send up to 10 times with a 1 s
// select each, and no HTTPClient setting governs that. A very bad link can
// therefore still stall one cycle longer than the budget.
#define LINK_FRAMES_POST_CONNECT_TIMEOUT_MS 1500
#define LINK_FRAMES_POST_RESPONSE_TIMEOUT_MS 2500
#define LINK_FRAMES_POST_BUDGET_MS 4000

static_assert(LINK_FRAMES_POST_CONNECT_TIMEOUT_MS + LINK_FRAMES_POST_RESPONSE_TIMEOUT_MS <= LINK_FRAMES_POST_BUDGET_MS,
              "connect plus response timeout must fit the frames POST budget");

// Writes `HTTP POST failed: code=<httpCode>`. The numeric HTTPClient code is
// printed on purpose: HTTPClient reports every connect failure, timeouts
// included, as the same -1 code, so a text conversion would mislead. There is
// deliberately no URL parameter. Same NULL and size contract as above.
static inline int linkFormatPostFailLog(char *out, size_t outSize, int httpCode) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "HTTP POST failed: code=%d", httpCode);
}

// ---- Runtime WiFi loss -----------------------------------------------------
//
// About 2 minutes of continuous WiFi loss while the firmware runs, then it
// reopens the setup portal instead of retrying forever.
#define LINK_LOSS_PORTAL_TIMEOUT_MS 120000

struct LinkLossTracker {
    bool down = false;
    uint32_t downSinceMs = 0;
};

// Feeds one link observation. Link up clears the state and returns false. The
// first down observation starts the clock. Returns true once the unsigned
// elapsed time since then is at least timeoutMs, and keeps returning true on
// later calls (the caller latches the reopen).
static inline bool linkLossTimedOut(LinkLossTracker &t, bool linkUp, uint32_t nowMs, uint32_t timeoutMs) {
    if (linkUp) {
        t.down = false;
        t.downSinceMs = 0;
        return false;
    }
    if (!t.down) {
        t.down = true;
        t.downSinceMs = nowMs;
    }
    return (uint32_t)(nowMs - t.downSinceMs) >= timeoutMs;
}

// Milliseconds the link has been down, 0 while it is up.
static inline uint32_t linkLossDownMs(const LinkLossTracker &t, uint32_t nowMs) {
    return t.down ? (uint32_t)(nowMs - t.downSinceMs) : 0;
}

// Writes `WiFi: link down for <s> s, reopening portal` with whole seconds
// (rounded down). Numbers only, no string parameter. Same NULL and size
// contract as the formatters above.
static inline int linkFormatLossLog(char *out, size_t outSize, uint32_t downMs) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "WiFi: link down for %lu s, reopening portal", (unsigned long)(downMs / 1000u));
}

#endif // LINK_STABILITY_H
