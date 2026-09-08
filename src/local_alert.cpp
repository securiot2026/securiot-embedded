#include "local_alert.h"

#include <Arduino.h>

namespace {

// GPIO assignments, must stay in sync with docs/PINOUT.md.
// GPIO4 doubles as the AI-Thinker board's onboard flash LED, reused here as
// the local-alert LED instead of wiring a separate external one, see
// docs/PINOUT.md for the reasoning.
const int kLedPin = 4;
const int kBuzzerPin = 2;

const unsigned long kBuzzerPulseMs = 300;
const unsigned int kBuzzerToneHz = 2000;
const unsigned int kBuzzerResolutionBits = 8;

} // namespace

void alertInit() {
    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, LOW);

    // Attach the buzzer pin to the LEDC peripheral so ledcWriteTone() can
    // drive it, the ESP32 Arduino core has no AVR-style tone()/noTone().
    ledcAttach(kBuzzerPin, kBuzzerToneHz, kBuzzerResolutionBits);
    ledcWriteTone(kBuzzerPin, 0);
}

void triggerLocalAlert() {
    digitalWrite(kLedPin, HIGH);
    ledcWriteTone(kBuzzerPin, kBuzzerToneHz);
    delay(kBuzzerPulseMs);
    ledcWriteTone(kBuzzerPin, 0);
}

void clearLocalAlert() {
    digitalWrite(kLedPin, LOW);
    ledcWriteTone(kBuzzerPin, 0);
}
