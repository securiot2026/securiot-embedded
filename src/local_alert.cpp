#include "local_alert.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "alert_state.h"

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
Outputs g_last = {{0, 0, 0}, false};
bool g_lastValid = false;

void setPixelColor(uint8_t r, uint8_t g, uint8_t b) {
    g_pixel.setPixelColor(0, g_pixel.Color(r, g, b));
    g_pixel.show();
    // A direct paint (Wi-Fi status, portal, fatal error) makes the cached
    // outputs stale, so the next applyOutputs() must repaint.
    g_lastValid = false;
}

// Single source of truth for what the LED and buzzer show: computeOutputs()
// composes manual holds, rule effects, the generic alert and the idle color.
// Hardware is only touched when the result changed (or `force` is set).
void applyOutputs(uint32_t nowMs, bool force) {
    Outputs o = computeOutputs(g_state, nowMs);
    if (!force && g_lastValid && o == g_last) {
        return;
    }
    setPixelColor(o.led.r, o.led.g, o.led.b);
    ledcWriteTone(kBuzzerPin, o.buzzer ? kBuzzerToneHz : 0);
    g_last = o;
    g_lastValid = true;
}

} // namespace

void alertInit() {
    g_pixel.begin();
    g_pixel.setBrightness(kBrightness);
    indicateWifiConnecting();

    // The ESP32 Arduino core has no AVR-style tone()/noTone(), LEDC drives
    // the buzzer directly.
    ledcAttach(kBuzzerPin, kBuzzerToneHz, kBuzzerResolutionBits);
    ledcWriteTone(kBuzzerPin, 0);
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
    setPixelColor(0, 0, 255);
}

void indicateWifiConnected() {
    // Idle color is green; force the repaint so a held manual light is
    // reasserted after a Wi-Fi reconnect painted the status colors over it.
    g_state.idleColor = Rgb{0, 255, 0};
    applyOutputs(millis(), true);
}

void indicatePortalActive() {
    // Non-blocking pulsing blue (0-1500ms rising, 1500-3000ms falling, no
    // delay()): this function is called every loop() iteration while the
    // portal is active, a blocking delay() here would stall
    // espConnect.loop() and the non-blocking portal. Distinct from
    // indicateWifiConnecting()'s solid blue.
    const unsigned long kPeriodMs = 3000;
    const uint8_t kMinBrightness = 20;
    const uint8_t kMaxBrightness = 100;

    unsigned long phase = millis() % kPeriodMs;
    unsigned long halfPeriod = kPeriodMs / 2;
    unsigned long triangle = (phase < halfPeriod) ? phase : (kPeriodMs - phase);
    uint8_t brightness = kMinBrightness +
                          (uint8_t)((triangle * (kMaxBrightness - kMinBrightness)) / halfPeriod);

    setPixelColor(0, 0, brightness);
}

void indicateFatalError() {
    while (true) {
        setPixelColor(255, 0, 0);
        delay(300);
        setPixelColor(0, 0, 0);
        delay(300);
    }
}
