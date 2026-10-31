#ifndef PROVISIONING_GUARD_H
#define PROVISIONING_GUARD_H

// Pure, Arduino-free helpers for the provisioning robustness fixes.
// Header-only, standard headers only, so the same code compiles into the
// firmware and into the host-side Unity tests (`pio test -e native`).
//
// Everything here exists so the decisions and the log lines of the setup flow
// can be tested without hardware, and so no log line can carry a secret: the
// formatters below only accept numbers and enums, never a URL, a network name,
// a password or a key.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---- Edge API ping budget -------------------------------------------------
//
// POST /provision-extra runs the Edge API reachability check inside the
// AsyncWebServer async_tcp task. PROVISIONING_TASK_WDT_MS is the task watchdog
// of that task, the one the real board hit (`E task_wdt: async_tcp, Rebooting`).
// The check must finish well under it, so the two HTTPClient timeouts below add
// up to at most PROVISIONING_PING_BUDGET_MS. The DNS lookup of a hostname is NOT
// covered by these timeouts (the form asks for an IP style URL, accepted risk).
#define PROVISIONING_PING_CONNECT_TIMEOUT_MS 1500
#define PROVISIONING_PING_RESPONSE_TIMEOUT_MS 1500
#define PROVISIONING_PING_BUDGET_MS 3000
#define PROVISIONING_TASK_WDT_MS 5000

static_assert(PROVISIONING_PING_CONNECT_TIMEOUT_MS + PROVISIONING_PING_RESPONSE_TIMEOUT_MS <= PROVISIONING_PING_BUDGET_MS,
              "connect plus response timeout must fit the ping budget");
static_assert(PROVISIONING_PING_BUDGET_MS + 1500 <= PROVISIONING_TASK_WDT_MS,
              "the ping budget must stay at least 1500 ms below the async_tcp task watchdog");

// Any HTTP status greater than 0 means the URL answered HTTP. Zero and the
// negative HTTPClient error codes (-1 refused, -4 not connected, -11 read
// timeout, and so on) mean unreachable.
static inline bool provisioningPingReachable(int httpCode) {
    return httpCode > 0;
}

// Elapsed milliseconds between two millis() readings. Unsigned subtraction
// stays correct across the 32-bit wrap (about every 49.7 days).
static inline uint32_t provisioningElapsedMs(uint32_t startMs, uint32_t endMs) {
    return endMs - startMs;
}

// Writes `Provisioning: Edge API ping code=<code> elapsed_ms=<ms>` into `out`.
// There is deliberately no URL parameter: the Edge API URL can carry the device
// key, so it must never reach a log line. Always NUL-terminates when outSize is
// at least 1, writes nothing for a NULL out or outSize 0. Returns the snprintf
// result (0 for the no-op cases).
static inline int provisioningFormatPingLog(char *out, size_t outSize, int httpCode, uint32_t elapsedMs) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "Provisioning: Edge API ping code=%d elapsed_ms=%lu", httpCode, (unsigned long)elapsedMs);
}

// ---- Boot decision --------------------------------------------------------
//
// What provisioningBegin() does at boot, given whether OUR config (namespace
// `securiot`, loaded by provisioningLoadConfig) is complete and whether the
// caller forces a portal reopen.
enum ProvisioningBootAction {
    // Complete config and no force: the portal library must not even start
    // (the "Preferences first" rule).
    PROVISIONING_BOOT_SKIP_PORTAL = 0,
    // Forced reopen while our config is complete (stale credentials): nothing
    // to clear, just open the portal.
    PROVISIONING_BOOT_START_PORTAL = 1,
    // Our config is missing or has an empty SSID: any WiFi settings the portal
    // library persisted on its own are stale by definition. Clear them, then
    // open the portal.
    PROVISIONING_BOOT_CLEAR_STALE_AND_START_PORTAL = 2
};

static inline ProvisioningBootAction provisioningDecideBoot(bool ourConfigComplete, bool force) {
    if (!ourConfigComplete) {
        return PROVISIONING_BOOT_CLEAR_STALE_AND_START_PORTAL;
    }
    return force ? PROVISIONING_BOOT_START_PORTAL : PROVISIONING_BOOT_SKIP_PORTAL;
}

// ---- PORTAL_COMPLETE verdict ----------------------------------------------

enum ProvisioningPortalVerdict {
    PROVISIONING_PORTAL_ACCEPT = 0,
    PROVISIONING_PORTAL_REJECT_AP_MODE = 1,
    PROVISIONING_PORTAL_REJECT_NO_SSID = 2
};

// Classifies the config the portal library hands over at PORTAL_COMPLETE.
// Two rejections:
//  - AP mode (the `AP mode` option of the portal page) has no SSID, no
//    credential test and no captive portal, it is a plain AP on 192.168.4.1.
//    It wins over the empty SSID check because AP mode always has an empty SSID.
//  - An empty SSID would be saved as configured with nothing to connect to,
//    which provisioningLoadConfig refuses, recreating the silent wait.
static inline ProvisioningPortalVerdict provisioningClassifyPortalComplete(bool apMode, size_t ssidLen) {
    if (apMode) {
        return PROVISIONING_PORTAL_REJECT_AP_MODE;
    }
    if (ssidLen == 0) {
        return PROVISIONING_PORTAL_REJECT_NO_SSID;
    }
    return PROVISIONING_PORTAL_ACCEPT;
}

static inline const char *provisioningPortalVerdictName(ProvisioningPortalVerdict verdict) {
    switch (verdict) {
        case PROVISIONING_PORTAL_ACCEPT:
            return "accept";
        case PROVISIONING_PORTAL_REJECT_AP_MODE:
            return "ap_mode";
        case PROVISIONING_PORTAL_REJECT_NO_SSID:
            return "no_ssid";
    }
    return "unknown";
}

// Writes `Provisioning: PORTAL_COMPLETE rejected reason=<name>, restarting portal`
// for a rejected verdict. Fed only with fixed verdict names, never a credential.
// ACCEPT writes an empty string and no line. NUL-terminates when outSize is at
// least 1, writes nothing for a NULL out or outSize 0.
static inline int provisioningFormatPortalRejectLog(char *out, size_t outSize, ProvisioningPortalVerdict verdict) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    if (verdict == PROVISIONING_PORTAL_ACCEPT) {
        out[0] = '\0';
        return 0;
    }
    return snprintf(out, outSize, "Provisioning: PORTAL_COMPLETE rejected reason=%s, restarting portal", provisioningPortalVerdictName(verdict));
}

#endif // PROVISIONING_GUARD_H
