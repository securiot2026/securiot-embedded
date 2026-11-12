#ifndef PORTAL_DIAG_H
#define PORTAL_DIAG_H

// Pure, Arduino-free helpers for the provisioning portal diagnostics log.
// Header-only, standard headers only, so the same code compiles into the
// firmware and into the host-side Unity tests (`pio test -e native`).
//
// Everything here exists to keep the Serial log useful and secret-free: the
// request path is reduced to printable ASCII and capped, a client MAC is cut
// to its last 3 bytes, and WiFi status codes get readable names.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PORTAL_DIAG_PATH_MAX_LEN 64
#define PORTAL_DIAG_PATH_BUF_LEN 65

// Copies the path part of a request URL into `out` for logging.
//
// Why it exists: the web server URL-decodes the path, so a hostile client on
// the open setup network could send %0D%0A or %1B and inject control or escape
// bytes (forged log lines, terminal tricks) into the Serial log. Every byte
// outside printable ASCII 0x20..0x7E becomes a period. Copying also stops at a
// question mark or a hash sign, so a query string (which can carry the device
// key) is never copied even if a caller passes a full URL.
//
// The visible output is capped at min(PORTAL_DIAG_PATH_MAX_LEN, outSize - 1)
// characters. A longer path keeps its first (cap - 3) characters and ends with
// three periods (a cap below 4 just truncates). The output is always
// NUL-terminated when outSize is at least 1 and nothing is written beyond
// outSize. Returns the output length (0 for NULL input or outSize 0).
static inline size_t portalDiagSanitizePath(const char *rawPath, char *out, size_t outSize) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    out[0] = '\0';
    if (rawPath == NULL) {
        return 0;
    }

    size_t cap = outSize - 1;
    if (cap > PORTAL_DIAG_PATH_MAX_LEN) {
        cap = PORTAL_DIAG_PATH_MAX_LEN;
    }

    // Length of the path part (up to NUL, '?' or '#').
    size_t pathLen = 0;
    while (rawPath[pathLen] != '\0' && rawPath[pathLen] != '?' && rawPath[pathLen] != '#') {
        ++pathLen;
    }

    const bool tooLong = pathLen > cap;
    size_t keep = tooLong ? (cap >= 4 ? cap - 3 : cap) : pathLen;
    size_t written = 0;
    for (size_t i = 0; i < keep; ++i) {
        const unsigned char c = (unsigned char)rawPath[i];
        out[written++] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';
    }
    if (tooLong && cap >= 4) {
        out[written++] = '.';
        out[written++] = '.';
        out[written++] = '.';
    }
    out[written] = '\0';
    return written;
}

// True when `path` is one of the connectivity-check paths phones and laptops
// request to detect a captive portal. Exact, case-sensitive match. The first
// five are the ones an operator looks for (Android, Apple, Windows), the rest
// are other probe paths the portal library also answers.
static inline bool portalDiagIsProbePath(const char *path) {
    if (path == NULL) {
        return false;
    }
    static const char *const kProbePaths[] = {
        "/generate_204",    "/hotspot-detect.html", "/connecttest.txt",
        "/ncsi.txt",        "/fwlink",              "/gen_204",
        "/library/test/success.html", "/redirect",  "/canonical.html",
        "/success.txt",     "/wpad.dat",            "/startpage",
    };
    for (size_t i = 0; i < sizeof(kProbePaths) / sizeof(kProbePaths[0]); ++i) {
        if (strcmp(path, kProbePaths[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Writes the last 3 bytes of a 6 byte MAC as "ab:cd:ef" (lower case hex). The
// vendor half of the address is never printed. A NULL mac writes "??:??:??".
// Needs an output buffer of at least 9 bytes; a smaller one gets an empty
// string and nothing is written beyond outSize. Returns `out`.
static inline char *portalDiagFormatMacSuffix(const uint8_t *mac6, char *out, size_t outSize) {
    if (out == NULL || outSize == 0) {
        return out;
    }
    if (outSize < 9) {
        out[0] = '\0';
        return out;
    }
    if (mac6 == NULL) {
        memcpy(out, "??:??:??", 9);
        return out;
    }
    static const char kHex[] = "0123456789abcdef";
    size_t pos = 0;
    for (int i = 3; i < 6; ++i) {
        out[pos++] = kHex[(mac6[i] >> 4) & 0x0F];
        out[pos++] = kHex[mac6[i] & 0x0F];
        if (i < 5) {
            out[pos++] = ':';
        }
    }
    out[pos] = '\0';
    return out;
}

// Wrap-safe "has this interval passed" check for millis() style counters.
// Unsigned subtraction keeps it correct across the 32-bit rollover.
static inline bool portalDiagIntervalElapsed(uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs) {
    return (uint32_t)(nowMs - lastMs) >= intervalMs;
}

// Names for WiFi.status() values. The numbers mirror wl_status_t in the Arduino
// core's WiFiType.h (provisioning.cpp static_asserts the ones it depends on).
static inline const char *portalDiagWifiStatusName(int status) {
    switch (status) {
        case 0:
            return "WL_IDLE_STATUS";
        case 1:
            return "WL_NO_SSID_AVAIL";
        case 2:
            return "WL_SCAN_COMPLETED";
        case 3:
            return "WL_CONNECTED";
        case 4:
            return "WL_CONNECT_FAILED";
        case 5:
            return "WL_CONNECTION_LOST";
        case 6:
            return "WL_DISCONNECTED";
        case 254:
            return "WL_STOPPED";
        case 255:
            return "WL_NO_SHIELD";
        default:
            return "WL_UNKNOWN";
    }
}

// True for the three WiFi statuses (WL_NO_SSID_AVAIL 1, WL_CONNECT_FAILED 4,
// WL_CONNECTION_LOST 5) on which MycilaESPConnect's credential test answers the
// portal with HTTP 400. These values mirror the switch in the library.
static inline bool portalDiagIsCredentialTestFailureStatus(int status) {
    return status == 1 || status == 4 || status == 5;
}

#endif // PORTAL_DIAG_H
