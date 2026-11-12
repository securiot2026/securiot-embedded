#ifndef OTA_POLICY_H
#define OTA_POLICY_H

// Pure, Arduino-free decisions and log formatters for the ArduinoOTA service.
// Header-only, standard headers only, so the same code compiles into the
// firmware (ota_update.cpp) and into the host-side Unity tests
// (`pio test -e native`).
//
// The formatters only accept numbers, never a string, so no line built here can
// carry the password, a URL or a network name.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define OTA_HOSTNAME "securiot-cam"
#define OTA_PORT 3232
#define OTA_PROGRESS_STEP_PERCENT 10

#define OTA_LOG_STARTED "OTA: update started"
#define OTA_LOG_FINISHED "OTA: update finished, restarting"
#define OTA_LOG_STOPPED "OTA: stopped, the setup portal is open"
// The only line allowed to name the macro, it never carries a value.
#define OTA_LOG_DISABLED "OTA: disabled, OTA_PASSWORD is not set in secrets.h"

// An unusable password (NULL or empty) means OTA must stay off. There is no
// empty-password mode: anyone on the LAN could flash the board.
static inline bool otaPasswordUsable(const char *password) {
    return password != NULL && password[0] != '\0';
}

enum OtaAction {
    OTA_ACTION_NONE = 0,
    OTA_ACTION_START,
    OTA_ACTION_STOP
};

// Start only with a usable password, an STA link and no setup portal (the
// portal access point is open, the update port must not be reachable from it).
// Stop only when the portal opens. A transient STA loss does not stop a running
// service: the UDP socket and the mDNS announcement come back with the link,
// and ending the service would tear down mDNS on every flap.
static inline OtaAction otaNextAction(bool passwordUsable, bool running, bool staUp, bool portalStarted) {
    if (!passwordUsable) {
        return OTA_ACTION_NONE;
    }
    if (!running) {
        return (staUp && !portalStarted) ? OTA_ACTION_START : OTA_ACTION_NONE;
    }
    return portalStarted ? OTA_ACTION_STOP : OTA_ACTION_NONE;
}

// Coarse progress reporting. lastStep starts at 0, so the first 0 percent report
// is silent.
struct OtaProgress {
    int lastStep = 0;
};

static inline void otaProgressReset(OtaProgress &p) {
    p.lastStep = 0;
}

// Returns the new multiple of OTA_PROGRESS_STEP_PERCENT when a higher step was
// crossed, and -1 otherwise (same step, lower step, total 0). done is clamped to
// total. 64-bit math, so a large image size cannot overflow.
static inline int otaProgressStep(OtaProgress &p, uint32_t done, uint32_t total) {
    if (total == 0) {
        return -1;
    }
    if (done > total) {
        done = total;
    }
    uint64_t pct = ((uint64_t)done * 100u) / (uint64_t)total;
    int step = (int)(pct / OTA_PROGRESS_STEP_PERCENT) * OTA_PROGRESS_STEP_PERCENT;
    if (step > p.lastStep) {
        p.lastStep = step;
        return step;
    }
    return -1;
}

// Maps the ArduinoOTA ota_error_t numbers. ota_update.cpp static_asserts the enum
// values, like the wl_status_t asserts in provisioning.cpp.
static inline const char *otaErrorName(int code) {
    switch (code) {
        case 0: return "auth";
        case 1: return "begin";
        case 2: return "connect";
        case 3: return "receive";
        case 4: return "end";
        default: return "unknown";
    }
}

// Writes `OTA: progress <percent>%`. Always NUL-terminates when outSize is at
// least 1, writes nothing for a NULL out or outSize 0. Returns the snprintf
// result (0 for the no-op cases).
static inline int otaFormatProgressLog(char *out, size_t outSize, int percent) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "OTA: progress %d%%", percent);
}

// Writes `OTA: error code=<code> (<name>)`. Same NULL and size contract.
static inline int otaFormatErrorLog(char *out, size_t outSize, int code) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "OTA: error code=%d (%s)", code, otaErrorName(code));
}

// Writes `OTA: ready host=securiot-cam.local ip=<a.b.c.d> port=3232`. The octets
// are numbers. Same NULL and size contract.
static inline int otaFormatReadyLog(char *out, size_t outSize, unsigned a, unsigned b, unsigned c, unsigned d) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "OTA: ready host=" OTA_HOSTNAME ".local ip=%u.%u.%u.%u port=%u", a, b, c, d,
                    (unsigned)OTA_PORT);
}

#endif // OTA_POLICY_H
