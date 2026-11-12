#ifndef BUTTON_STATE_H
#define BUTTON_STATE_H

// Pure, Arduino-free state machine for the reset and pairing button on GPIO12.
// Header-only, standard headers only, so the same code compiles into the
// firmware and into the host-side Unity tests (`pio test -e native`).
//
// The log formatter only accepts numbers and enums, never a string, so no log
// line built here can carry a URL, a key, a network name or a password.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Contact bounce filter: a raw level must stay the same this long to count.
#define BUTTON_DEBOUNCE_MS 30
// The LED starts the magenta countdown after this much hold time.
#define BUTTON_COUNTDOWN_START_MS 500
// A hold of this length (measured from the press edge) is a long press.
#define BUTTON_LONG_PRESS_MS 3000
// After the config is erased the white blink stays at least this long, so the
// operator sees the confirmation even if they already let go.
#define BUTTON_RESET_MIN_CONFIRM_MS 800
// Ceiling for the same wait when the operator keeps holding the button.
#define BUTTON_RESET_MAX_WAIT_MS 5000

static_assert(BUTTON_DEBOUNCE_MS < BUTTON_COUNTDOWN_START_MS, "debounce must be shorter than the countdown start");
static_assert(BUTTON_COUNTDOWN_START_MS < BUTTON_LONG_PRESS_MS, "countdown must start before the long press");
static_assert(BUTTON_RESET_MIN_CONFIRM_MS < BUTTON_RESET_MAX_WAIT_MS, "min confirm must be below the max wait");

enum ButtonEvent {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS
};

enum ButtonPhase {
    BUTTON_PHASE_IDLE = 0,
    BUTTON_PHASE_PRESSED,
    BUTTON_PHASE_COUNTDOWN,
    BUTTON_PHASE_CONFIRMED
};

struct ButtonTracker {
    bool seeded = false;
    bool rawPressed = false;
    uint32_t rawChangeMs = 0;
    bool stablePressed = false;
    uint32_t pressStartMs = 0;
    bool longFired = false;
    bool heldFromBoot = false;
};

struct ButtonStatus {
    ButtonEvent event = BUTTON_EVENT_NONE;
    ButtonPhase phase = BUTTON_PHASE_IDLE;
    uint32_t heldMs = 0;
    bool fromBoot = false;
};

// Feeds one raw reading (true = pressed, the pin is LOW) into the tracker.
//
// Hold time is measured from the press edge (the first raw reading of the
// accepted press) with unsigned subtraction, so it stays correct when millis()
// wraps. A press that starts while the main loop is blocked is only seen at the
// next poll, so its hold time counts from that first observation.
//
// LONG_PRESS fires once per hold and only while the raw level is still pressed,
// so a release or a sparse poll can never turn into a long press.
static inline ButtonStatus buttonUpdate(ButtonTracker &t, uint32_t nowMs, bool rawPressed) {
    ButtonStatus s;

    if (!t.seeded) {
        t.seeded = true;
        t.rawPressed = rawPressed;
        t.rawChangeMs = nowMs;
        t.stablePressed = rawPressed;
        t.longFired = false;
        if (rawPressed) {
            t.pressStartMs = nowMs;
            t.heldFromBoot = true;
            s.phase = BUTTON_PHASE_PRESSED;
            s.fromBoot = true;
        }
        return s;
    }

    if (rawPressed != t.rawPressed) {
        t.rawPressed = rawPressed;
        t.rawChangeMs = nowMs;
    }

    if (t.rawPressed != t.stablePressed && (uint32_t)(nowMs - t.rawChangeMs) >= BUTTON_DEBOUNCE_MS) {
        t.stablePressed = t.rawPressed;
        if (t.stablePressed) {
            t.pressStartMs = t.rawChangeMs;
            t.longFired = false;
            t.heldFromBoot = false;
        } else {
            s.fromBoot = t.heldFromBoot;
            if (!t.longFired) {
                uint32_t held = (uint32_t)(t.rawChangeMs - t.pressStartMs);
                if (held > BUTTON_LONG_PRESS_MS - 1) {
                    held = BUTTON_LONG_PRESS_MS - 1;
                }
                s.event = BUTTON_EVENT_SHORT_PRESS;
                s.heldMs = held;
            }
            s.phase = BUTTON_PHASE_IDLE;
            t.longFired = false;
            t.heldFromBoot = false;
            return s;
        }
    }

    if (!t.stablePressed) {
        return s;
    }

    s.heldMs = (uint32_t)(nowMs - t.pressStartMs);
    s.fromBoot = t.heldFromBoot;
    if (t.longFired) {
        s.phase = BUTTON_PHASE_CONFIRMED;
    } else if (t.rawPressed && s.heldMs >= BUTTON_LONG_PRESS_MS) {
        t.longFired = true;
        s.event = BUTTON_EVENT_LONG_PRESS;
        s.phase = BUTTON_PHASE_CONFIRMED;
    } else if (s.heldMs >= BUTTON_COUNTDOWN_START_MS) {
        s.phase = BUTTON_PHASE_COUNTDOWN;
    } else {
        s.phase = BUTTON_PHASE_PRESSED;
    }
    return s;
}

// Writes one fixed-text log line for a button event. NONE writes an empty
// string and returns 0. Always NUL-terminates when outSize is at least 1,
// writes nothing for a NULL out or outSize 0. Returns the snprintf result.
static inline int buttonFormatEventLog(char *out, size_t outSize, ButtonEvent event, uint32_t heldMs, bool fromBoot) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            return snprintf(out, outSize, "Button: short press held_ms=%lu, no action", (unsigned long)heldMs);
        case BUTTON_EVENT_LONG_PRESS:
            if (fromBoot) {
                return snprintf(out, outSize,
                                "Button: held at boot, long press confirmed held_ms=%lu, erasing config and restarting into portal",
                                (unsigned long)heldMs);
            }
            return snprintf(out, outSize,
                            "Button: long press confirmed held_ms=%lu, erasing config and restarting into portal",
                            (unsigned long)heldMs);
        case BUTTON_EVENT_NONE:
            break;
    }
    out[0] = '\0';
    return 0;
}

// True when the wait after the erase may end: never before the minimum confirm
// time, then as soon as the button is released, and at the latest at the
// ceiling. The ceiling exists so a user who keeps holding after the
// confirmation cannot block the restart forever.
static inline bool buttonResetWaitDone(uint32_t nowMs, uint32_t startMs, bool stillPressed) {
    uint32_t elapsed = (uint32_t)(nowMs - startMs);
    if (elapsed < BUTTON_RESET_MIN_CONFIRM_MS) {
        return false;
    }
    if (!stillPressed) {
        return true;
    }
    return elapsed >= BUTTON_RESET_MAX_WAIT_MS;
}

#endif // BUTTON_STATE_H
