#include "local_alert.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "alert_state.h"
#include "button_state.h"
#include "led_pattern.h"

namespace {

// GPIO assignments, must stay in sync with docs/PINOUT.md.
// GPIO4 is the AI-Thinker board's onboard flash LED pad, reused here to
// drive an external WS2812 status LED instead: same pin, same zero extra
// GPIO cost, but color-coded status (blue/green/red) instead of a plain
// white flash. The onboard flash LED itself is not usable in parallel,
// driving GPIO4 as WS2812 data overrides it, see docs/WIRING.md.
const int kBuzzerPin = 2;
const int kRgbLedPin = 4;
const int kNumPixels = 1;

const unsigned long kBuzzerPulseMs = 300;
const unsigned int kBuzzerToneHz = 2000;
const unsigned int kBuzzerResolutionBits = 8;

// Keeps current draw low on a 5V rail already shared with 3 servos, still
// plenty visible indoors at the detection range this project targets.
const uint8_t kBrightness = 40;

Adafruit_NeoPixel g_pixel(kNumPixels, kRgbLedPin, NEO_GRB + NEO_KHZ800);

AlertState g_state;

// What the device is doing, set by the indicate* functions below. CONNECTED
// means "show the composed alert colors" (manual light, rule light, alert, idle).
LedSystem g_system = LED_SYSTEM_CONNECTING;

// Long-press overlay from reset_button.cpp, shown on top of every system state.
ButtonPhase g_buttonPhase = BUTTON_PHASE_IDLE;
uint32_t g_buttonHeldMs = 0;

// Last values written to the hardware, so a call that changes nothing does not
// repaint the pixel or rewrite the buzzer tone.
Rgb g_lastLed = {0, 0, 0};
bool g_lastLedValid = false;
bool g_lastBuzzer = false;
bool g_lastBuzzerValid = false;

void paintPixel(const Rgb &c) {
    g_pixel.setPixelColor(0, g_pixel.Color(c.r, c.g, c.b));
    g_pixel.show();
}

// The only place that touches the pixel and the buzzer. computeOutputs()
// composes manual holds, rule effects, the generic alert and the idle color,
// ledPatternColor() puts the system state and the button overlay on top.
// Hardware is only written when the result changed (or `force` is set).
void applyOutputs(uint32_t nowMs, bool force) {
    const Outputs o = computeOutputs(g_state, nowMs);

    LedInput in;
    in.system = g_system;
    in.button = g_buttonPhase;
    in.heldMs = g_buttonHeldMs;
    in.composed = o.led;
    const Rgb color = ledPatternColor(in, nowMs);

    if (force || !g_lastLedValid || color != g_lastLed) {
        paintPixel(color);
        g_lastLed = color;
        g_lastLedValid = true;
    }
    if (force || !g_lastBuzzerValid || o.buzzer != g_lastBuzzer) {
        ledcWriteTone(kBuzzerPin, o.buzzer ? kBuzzerToneHz : 0);
        g_lastBuzzer = o.buzzer;
        g_lastBuzzerValid = true;
    }
}

} // namespace

void alertInit() {
    g_pixel.begin();
    g_pixel.setBrightness(kBrightness);

    // The ESP32 Arduino core has no AVR-style tone()/noTone(), LEDC drives
    // the buzzer directly. Attached before the first applyOutputs() writes it.
    ledcAttach(kBuzzerPin, kBuzzerToneHz, kBuzzerResolutionBits);
    ledcWriteTone(kBuzzerPin, 0);
    indicateWifiConnecting();
}

void triggerLocalAlert() {
    g_state.alert = true;
    applyOutputs(millis(), false);

    // Blocking confirmation beep. Afterwards the composed state is re-applied
    // with force so a held manual buzzer or rule buzzer tone is restored (and
    // an idle buzzer goes silent).
    ledcWriteTone(kBuzzerPin, kBuzzerToneHz);
    delay(kBuzzerPulseMs);
    applyOutputs(millis(), true);
}

void clearLocalAlert() {
    // Only the generic alert flag is cleared. Manual light/buzzer holds and
    // rule effects have their own timers, so they are NOT cancelled here
    // (audit G4, D-05).
    g_state.alert = false;
    applyOutputs(millis(), false);
}

void setManualLight(bool on, const char *colorName, uint32_t ttlMs) {
    uint32_t now = millis();
    if (on) {
        g_state.manualLightColor = colorFromName(colorName, Rgb{255, 255, 255});
        g_state.manualLight.set(now, ttlMs == 0 ? kManualHoldTtlMs : ttlMs);
    } else {
        g_state.manualLight.clear();
    }
    applyOutputs(now, false);
}

void setManualBuzzer(bool on, uint32_t ttlMs) {
    uint32_t now = millis();
    if (on) {
        g_state.manualBuzzer.set(now, ttlMs == 0 ? kManualHoldTtlMs : ttlMs);
    } else {
        g_state.manualBuzzer.clear();
    }
    applyOutputs(now, false);
}

void applyRuleLight(bool on, const char *colorName) {
    uint32_t now = millis();
    if (on) {
        g_state.ruleLightColor = colorFromName(colorName, Rgb{255, 0, 0});
        g_state.ruleLight.set(now, kRuleEffectMs);
    } else {
        g_state.ruleLight.clear();
    }
    applyOutputs(now, false);
}

void applyRuleBuzzer(bool on) {
    uint32_t now = millis();
    if (on) {
        g_state.ruleBuzzer.set(now, kRuleEffectMs);
    } else {
        g_state.ruleBuzzer.clear();
    }
    applyOutputs(now, false);
}

void alertTick() {
    applyOutputs(millis(), false);
}

void indicateWifiConnecting() {
    g_system = LED_SYSTEM_CONNECTING;
    applyOutputs(millis(), false);
}

void indicateWifiConnected() {
    // Idle color is green; force the repaint so a held manual light is
    // reasserted after a Wi-Fi reconnect painted the status colors over it.
    g_state.idleColor = Rgb{0, 255, 0};
    g_system = LED_SYSTEM_CONNECTED;
    applyOutputs(millis(), true);
}

void indicatePortalActive(bool credentialsAccepted, int apClients) {
    // Non-blocking, no delay(). Refreshed on every loop() iteration while the
    // portal is active, which is what makes the pulse animate.
    g_system = ledSystemForPortal(credentialsAccepted, apClients);
    applyOutputs(millis(), false);
}

void indicateButtonProgress(ButtonPhase phase, uint32_t heldMs) {
    g_buttonPhase = phase;
    g_buttonHeldMs = heldMs;
    applyOutputs(millis(), false);
}

void indicateOtaActive() {
    g_system = LED_SYSTEM_OTA;
    applyOutputs(millis(), true);
}

void indicateFatalError() {
    g_system = LED_SYSTEM_FATAL;
    while (true) {
        applyOutputs(millis(), false);
        delay(10);
    }
}
