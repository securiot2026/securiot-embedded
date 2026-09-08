#include "local_alert.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

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

void setPixelColor(uint8_t r, uint8_t g, uint8_t b) {
    g_pixel.setPixelColor(0, g_pixel.Color(r, g, b));
    g_pixel.show();
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
    setPixelColor(255, 0, 0);
    ledcWriteTone(kBuzzerPin, kBuzzerToneHz);
    delay(kBuzzerPulseMs);
    ledcWriteTone(kBuzzerPin, 0);
}

void clearLocalAlert() {
    ledcWriteTone(kBuzzerPin, 0);
    indicateWifiConnected();
}

void indicateWifiConnecting() {
    setPixelColor(0, 0, 255);
}

void indicateWifiConnected() {
    setPixelColor(0, 255, 0);
}

void indicateFatalError() {
    while (true) {
        setPixelColor(255, 0, 0);
        delay(300);
        setPixelColor(0, 0, 0);
        delay(300);
    }
}
