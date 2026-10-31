#ifndef FRAME_RESPONSE_H
#define FRAME_RESPONSE_H

// Arduino-free parsing of the actuator-related parts of the Edge API's
// /frames JSON response: rule_command (Web rule light/buzzer actions),
// manual_command (Mobile commands, including the dry-run execute shapes
// light/buzzer with optional color and duration_s, and door_lock) and the
// optional faces array. Pure functions over a read-only ArduinoJson variant,
// fixed-size buffers, no dynamic allocation, so they can be tested on the host
// (`pio test -e native`). main.cpp applies the parsed values to the hardware.

#include <ArduinoJson.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "alert_state.h"

// Longest color text kept (named color or #RRGGBB). Longer values are ignored
// (the field stays empty and the caller's fallback color applies).
static const size_t kMaxColorLen = 16;

enum class ActuatorKind : uint8_t {
    Unknown = 0,
    Light,
    Buzzer,
    DoorLock,
    PanTilt,
};

struct RuleCommand {
    bool present = false;
    ActuatorKind actuator = ActuatorKind::Unknown;  // Light or Buzzer when present
    bool on = true;                                 // rule actions default to on
    char color[kMaxColorLen + 1] = {0};
};

struct ManualCommand {
    bool present = false;
    ActuatorKind actuator = ActuatorKind::Unknown;
    bool on = false;      // light/buzzer: missing flag means off (unchanged behavior)
    char color[kMaxColorLen + 1] = {0};
    uint32_t ttlMs = 0;   // 0 = use the default hold; otherwise clamped 1..300 s
    float panDelta = NAN;
    float tiltDelta = NAN;
};

struct FacesSummary {
    bool present = false;
    int count = 0;
    int known = 0;
};

namespace frame_response_detail {

// Copies action["color"] into dst when it is a string that fits; otherwise
// leaves dst empty.
inline void copyColor(JsonVariantConst action, char (&dst)[kMaxColorLen + 1]) {
    dst[0] = '\0';
    JsonVariantConst c = action["color"];
    if (!c.is<const char *>()) {
        return;
    }
    const char *s = c.as<const char *>();
    if (s == nullptr) {
        return;
    }
    size_t len = strnlen(s, kMaxColorLen + 1);
    if (len == 0 || len > kMaxColorLen) {
        return;
    }
    memcpy(dst, s, len);
    dst[len] = '\0';
}

inline bool readBool(JsonVariantConst v, bool fallback) {
    return v.is<bool>() ? v.as<bool>() : fallback;
}

}  // namespace frame_response_detail

// rule_command: {"actuator":"light"|"buzzer","action":{color?,on?},"rule":name}.
// Absent, null, non-object or unknown actuator -> present = false.
inline RuleCommand parseRuleCommand(JsonVariantConst v) {
    using namespace frame_response_detail;
    RuleCommand out;
    if (!v.is<JsonObjectConst>()) {
        return out;
    }
    const char *actuator = v["actuator"].is<const char *>() ? v["actuator"].as<const char *>() : nullptr;
    if (actuator == nullptr) {
        return out;
    }
    if (strcmp(actuator, "light") == 0) {
        out.actuator = ActuatorKind::Light;
    } else if (strcmp(actuator, "buzzer") == 0) {
        out.actuator = ActuatorKind::Buzzer;
    } else {
        return out;
    }
    out.present = true;
    JsonVariantConst action = v["action"];
    out.on = readBool(action["on"], true);
    if (out.actuator == ActuatorKind::Light) {
        copyColor(action, out.color);
    }
    return out;
}

// manual_command: {"actuator":...,"action":{...},"issued_at":...}.
// Absent or null -> present = false. A non-null command with an unknown
// actuator is present with actuator = Unknown.
inline ManualCommand parseManualCommand(JsonVariantConst v) {
    using namespace frame_response_detail;
    ManualCommand out;
    if (!v.is<JsonObjectConst>()) {
        return out;
    }
    out.present = true;
    const char *actuator = v["actuator"].is<const char *>() ? v["actuator"].as<const char *>() : "";
    JsonVariantConst action = v["action"];

    if (strcmp(actuator, "pan_tilt") == 0) {
        out.actuator = ActuatorKind::PanTilt;
        JsonVariantConst pan = action["pan_delta"];
        JsonVariantConst tilt = action["tilt_delta"];
        out.panDelta = pan.isNull() ? NAN : pan.as<float>();
        out.tiltDelta = tilt.isNull() ? NAN : tilt.as<float>();
    } else if (strcmp(actuator, "light") == 0 || strcmp(actuator, "buzzer") == 0) {
        bool isLight = strcmp(actuator, "light") == 0;
        out.actuator = isLight ? ActuatorKind::Light : ActuatorKind::Buzzer;
        out.on = readBool(action["on"], false);
        if (isLight) {
            copyColor(action, out.color);
        }
        JsonVariantConst d = action["duration_s"];
        if (d.is<double>()) {
            out.ttlMs = clampDurationMs(d.as<double>());
        }
    } else if (strcmp(actuator, "door_lock") == 0) {
        out.actuator = ActuatorKind::DoorLock;
    }
    return out;
}

// faces: array of {bbox, detection_confidence, identity, similarity, known}.
// Only counts are extracted: identity names and similarity scores are personal
// data and are deliberately never read.
inline FacesSummary parseFaces(JsonVariantConst v) {
    FacesSummary out;
    if (!v.is<JsonArrayConst>()) {
        return out;
    }
    out.present = true;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
        out.count++;
        if (frame_response_detail::readBool(item["known"], false)) {
            out.known++;
        }
    }
    return out;
}

#endif  // FRAME_RESPONSE_H
