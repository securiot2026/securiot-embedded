#ifndef ALERT_STATE_H
#define ALERT_STATE_H

// Arduino-free alert/actuator state engine: hold timers, color parsing and
// the composition of the status LED color and the buzzer from every active
// source. local_alert.cpp is thin hardware glue over this header so the
// priority and expiry rules can be tested on the host (`pio test -e native`).
//
// Priority for the LED (highest first): manual light hold, rule light
// effect, generic alert (red), idle (green). The buzzer is on while a manual
// buzzer hold or a rule buzzer effect is active; the generic alert flag does
// not hold the buzzer (the alert pulse is a blocking beep in local_alert.cpp).

#include <stddef.h>
#include <stdint.h>

// Default time a manual light/buzzer command is held when the caller gives no
// duration. Long enough to be useful, short enough that a lost "off" command
// cannot leave the buzzer on forever. Override with -DMANUAL_HOLD_TTL_MS=...
#ifndef MANUAL_HOLD_TTL_MS
#define MANUAL_HOLD_TTL_MS 120000u
#endif

static const uint32_t kManualHoldTtlMs = MANUAL_HOLD_TTL_MS;
static const uint32_t kRuleEffectMs = 5000u;
static const uint32_t kMaxHoldMs = 300000u;  // 300 s ceiling for any caller duration

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb &o) const { return !(*this == o); }
};

// Timer with wrap-safe arithmetic: elapsed time is computed with unsigned
// subtraction, so it stays correct when the 32-bit millis() counter wraps.
struct Hold {
    bool active = false;
    uint32_t startMs = 0;
    uint32_t ttlMs = 0;

    void set(uint32_t nowMs, uint32_t ttl) {
        active = true;
        startMs = nowMs;
        ttlMs = ttl;
    }
    void clear() { active = false; }
    bool isActive(uint32_t nowMs) const {
        return active && (uint32_t)(nowMs - startMs) < ttlMs;
    }
};

// Converts a caller-supplied duration in seconds to milliseconds.
// 0 or negative (or NaN) -> 0, meaning "use the default"; positive values are
// clamped to 1..300 s.
inline uint32_t clampDurationMs(double seconds) {
    if (!(seconds > 0.0)) {
        return 0;
    }
    if (seconds >= (double)(kMaxHoldMs / 1000u)) {
        return kMaxHoldMs;
    }
    if (seconds < 1.0) {
        return 1000u;
    }
    return (uint32_t)(seconds * 1000.0);
}

namespace alert_state_detail {

inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

// Case-insensitive compare of a bounded name against a lowercase literal.
inline bool equalsIgnoreCase(const char *name, size_t len, const char *lit) {
    size_t i = 0;
    for (; i < len; i++) {
        if (lit[i] == '\0' || lower(name[i]) != lit[i]) {
            return false;
        }
    }
    return lit[i] == '\0';
}

inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace alert_state_detail

// Parses a named color (case-insensitive) or #RRGGBB. Anything else (null,
// empty, malformed hex, unknown name, overlong text) returns the fallback.
// Never reads more than 16 characters.
inline Rgb colorFromName(const char *name, Rgb fallback) {
    using namespace alert_state_detail;
    if (name == nullptr) {
        return fallback;
    }
    size_t len = 0;
    while (len < 16 && name[len] != '\0') {
        len++;
    }
    if (len == 0 || len >= 16) {
        return fallback;
    }

    if (name[0] == '#') {
        if (len != 7) {
            return fallback;
        }
        int v[6];
        for (int i = 0; i < 6; i++) {
            v[i] = hexVal(name[1 + i]);
            if (v[i] < 0) {
                return fallback;
            }
        }
        Rgb c = {(uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]),
                 (uint8_t)(v[4] * 16 + v[5])};
        return c;
    }

    struct Named {
        const char *name;
        Rgb color;
    };
    static const Named kNamed[] = {
        {"red", {255, 0, 0}},     {"green", {0, 255, 0}},     {"blue", {0, 0, 255}},
        {"yellow", {255, 255, 0}}, {"orange", {255, 128, 0}}, {"purple", {128, 0, 255}},
        {"cyan", {0, 255, 255}},  {"magenta", {255, 0, 255}}, {"white", {255, 255, 255}},
    };
    for (size_t i = 0; i < sizeof(kNamed) / sizeof(kNamed[0]); i++) {
        if (equalsIgnoreCase(name, len, kNamed[i].name)) {
            return kNamed[i].color;
        }
    }
    return fallback;
}

struct AlertState {
    bool alert = false;  // generic detection alert flag (red)

    Hold manualLight;
    Rgb manualLightColor = {255, 255, 255};
    Hold manualBuzzer;

    Hold ruleLight;
    Rgb ruleLightColor = {255, 0, 0};
    Hold ruleBuzzer;

    Rgb idleColor = {0, 255, 0};
};

struct Outputs {
    Rgb led;
    bool buzzer;

    bool operator==(const Outputs &o) const { return led == o.led && buzzer == o.buzzer; }
    bool operator!=(const Outputs &o) const { return !(*this == o); }
};

inline Outputs computeOutputs(const AlertState &s, uint32_t nowMs) {
    Outputs o;
    if (s.manualLight.isActive(nowMs)) {
        o.led = s.manualLightColor;
    } else if (s.ruleLight.isActive(nowMs)) {
        o.led = s.ruleLightColor;
    } else if (s.alert) {
        o.led = Rgb{255, 0, 0};
    } else {
        o.led = s.idleColor;
    }
    o.buzzer = s.manualBuzzer.isActive(nowMs) || s.ruleBuzzer.isActive(nowMs);
    return o;
}

#endif  // ALERT_STATE_H
