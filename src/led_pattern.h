#ifndef LED_PATTERN_H
#define LED_PATTERN_H

// Pure, Arduino-free mapping from the device state to the WS2812 color.
// Header-only, so the same code compiles into the firmware and into the
// host-side Unity tests (`pio test -e native`). local_alert.cpp is the thin
// hardware glue that writes the returned color to the pixel.
//
// Priority (highest first): the button overlay (countdown, reset confirmed),
// then the system state (connecting, portal states, fatal), then the composed
// alert color. The overlay is the operator's own action, system states mean the
// device is not online yet, and only the connected state defers to alert_state.h
// so manual light, rule light, alert and idle keep their existing order.

#include <stdint.h>

#include "alert_state.h"
#include "button_state.h"

#define LED_BLINK_SLOW_PERIOD_MS 1200
#define LED_BLINK_FATAL_PERIOD_MS 600
#define LED_PULSE_PERIOD_MS 3000
#define LED_PULSE_MIN_LEVEL 51
#define LED_COUNTDOWN_PERIOD_START_MS 600
#define LED_COUNTDOWN_PERIOD_END_MS 120
#define LED_RESET_BLINK_PERIOD_MS 120

static const Rgb kLedBlue = {0, 0, 255};
static const Rgb kLedCyan = {0, 255, 255};
static const Rgb kLedYellow = {255, 255, 0};
static const Rgb kLedRed = {255, 0, 0};
static const Rgb kLedWhite = {255, 255, 255};
static const Rgb kLedMagenta = {255, 0, 255};
static const Rgb kLedOff = {0, 0, 0};

enum LedSystem {
    LED_SYSTEM_CONNECTED = 0,  // use the composed alert color
    LED_SYSTEM_CONNECTING,
    LED_SYSTEM_PORTAL_READY,
    LED_SYSTEM_PORTAL_CLIENT,
    LED_SYSTEM_PORTAL_CREDENTIALS,
    LED_SYSTEM_FATAL
};

struct LedInput {
    LedSystem system;
    ButtonPhase button;
    uint32_t heldMs;
    Rgb composed;
};

// True during the first half of each period. Period 0 is always on. The modulo
// has a one-time phase jump when millis() wraps every 49.7 days, harmless for a
// status blink.
static inline bool ledBlinkOn(uint32_t nowMs, uint32_t periodMs) {
    if (periodMs == 0) {
        return true;
    }
    return (nowMs % periodMs) < (periodMs / 2);
}

// Triangle wave: minLevel at phase 0, 255 at half period, back to minLevel.
static inline uint8_t ledPulseLevel(uint32_t nowMs, uint32_t periodMs, uint8_t minLevel) {
    if (periodMs < 2) {
        return 255;
    }
    uint32_t half = periodMs / 2;
    uint32_t phase = nowMs % periodMs;
    uint32_t tri = phase < half ? phase : periodMs - phase;
    if (tri > half) {
        tri = half;
    }
    uint32_t span = 255u - minLevel;
    return (uint8_t)(minLevel + (tri * span) / half);
}

static inline Rgb ledScale(Rgb color, uint8_t level) {
    Rgb out;
    out.r = (uint8_t)(((uint32_t)color.r * level) / 255u);
    out.g = (uint8_t)(((uint32_t)color.g * level) / 255u);
    out.b = (uint8_t)(((uint32_t)color.b * level) / 255u);
    return out;
}

// Blink period of the long-press countdown: linear from START at the countdown
// start down to END at the long press, clamped outside that range.
static inline uint32_t ledCountdownPeriodMs(uint32_t heldMs) {
    if (heldMs <= BUTTON_COUNTDOWN_START_MS) {
        return LED_COUNTDOWN_PERIOD_START_MS;
    }
    if (heldMs >= BUTTON_LONG_PRESS_MS) {
        return LED_COUNTDOWN_PERIOD_END_MS;
    }
    uint32_t range = BUTTON_LONG_PRESS_MS - BUTTON_COUNTDOWN_START_MS;
    uint32_t drop = LED_COUNTDOWN_PERIOD_START_MS - LED_COUNTDOWN_PERIOD_END_MS;
    return LED_COUNTDOWN_PERIOD_START_MS - ((heldMs - BUTTON_COUNTDOWN_START_MS) * drop) / range;
}

static inline LedSystem ledSystemForPortal(bool credentialsAccepted, int apClients) {
    if (credentialsAccepted) {
        return LED_SYSTEM_PORTAL_CREDENTIALS;
    }
    if (apClients > 0) {
        return LED_SYSTEM_PORTAL_CLIENT;
    }
    return LED_SYSTEM_PORTAL_READY;
}

// New SoftAP client count after a station connect or disconnect event. Never
// returns a negative value.
static inline int ledApClientsAfter(int current, bool connected) {
    if (current < 0) {
        current = 0;
    }
    if (connected) {
        return current + 1;
    }
    return current > 0 ? current - 1 : 0;
}

static inline Rgb ledPatternColor(const LedInput &in, uint32_t nowMs) {
    if (in.button == BUTTON_PHASE_COUNTDOWN) {
        return ledBlinkOn(nowMs, ledCountdownPeriodMs(in.heldMs)) ? kLedMagenta : kLedOff;
    }
    if (in.button == BUTTON_PHASE_CONFIRMED) {
        return ledBlinkOn(nowMs, LED_RESET_BLINK_PERIOD_MS) ? kLedWhite : kLedOff;
    }
    switch (in.system) {
        case LED_SYSTEM_CONNECTING:
            return ledBlinkOn(nowMs, LED_BLINK_SLOW_PERIOD_MS) ? kLedBlue : kLedOff;
        case LED_SYSTEM_PORTAL_READY:
            return ledScale(kLedCyan, ledPulseLevel(nowMs, LED_PULSE_PERIOD_MS, LED_PULSE_MIN_LEVEL));
        case LED_SYSTEM_PORTAL_CLIENT:
            return kLedCyan;
        case LED_SYSTEM_PORTAL_CREDENTIALS:
            return ledScale(kLedYellow, ledPulseLevel(nowMs, LED_PULSE_PERIOD_MS, LED_PULSE_MIN_LEVEL));
        case LED_SYSTEM_FATAL:
            return ledBlinkOn(nowMs, LED_BLINK_FATAL_PERIOD_MS) ? kLedRed : kLedOff;
        case LED_SYSTEM_CONNECTED:
        default:
            return in.composed;
    }
}

#endif // LED_PATTERN_H
