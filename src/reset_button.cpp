#include "reset_button.h"

#include <Arduino.h>

#include "button_state.h"
#include "local_alert.h"
#include "provisioning.h"

namespace {

// Must stay in sync with docs/PINOUT.md. GPIO12 is the MTDI strapping pin: LOW
// at reset selects 3.3V flash I/O, which this board needs. The button goes to
// GND only, which is LOW and safe at power-on (also when held during boot). Never
// add an external pull-up or an active-high button on this pin.
const int kResetButtonPin = 12;

ButtonTracker g_tracker;

bool readPressed() {
    return digitalRead(kResetButtonPin) == LOW;
}

// Erases the config, shows the fast white blink and restarts. Never returns.
// The wait is bounded by buttonResetWaitDone so a user who keeps holding cannot
// block the restart forever.
void eraseAndRestart() {
    provisioningEraseConfig();
    Serial.println("Button: config erased, release the button, restarting into the setup portal");

    const uint32_t startMs = millis();
    while (true) {
        const uint32_t now = millis();
        const ButtonStatus s = buttonUpdate(g_tracker, now, readPressed());
        // Forced so the white blink shows even if the user already let go.
        indicateButtonProgress(BUTTON_PHASE_CONFIRMED, s.heldMs);
        if (buttonResetWaitDone(now, startMs, s.phase != BUTTON_PHASE_IDLE)) {
            break;
        }
        delay(10);
    }

    Serial.flush();
    ESP.restart();
    while (true) {
        delay(1000);
    }
}

} // namespace

void resetButtonInit() {
    pinMode(kResetButtonPin, INPUT_PULLUP);
    delayMicroseconds(50);
    const ButtonStatus s = buttonUpdate(g_tracker, millis(), readPressed());
    Serial.println("Button: GPIO12 ready, hold 3 s to erase the config and open the portal");
    if (s.phase != BUTTON_PHASE_IDLE) {
        Serial.println("Button: held at boot, keep holding 3 s to erase the config");
    }
}

void resetButtonPoll() {
    const ButtonStatus s = buttonUpdate(g_tracker, millis(), readPressed());
    indicateButtonProgress(s.phase, s.heldMs);

    if (s.event == BUTTON_EVENT_NONE) {
        return;
    }

    char line[128];
    buttonFormatEventLog(line, sizeof(line), s.event, s.heldMs, s.fromBoot);
    Serial.println(line);

    if (s.event == BUTTON_EVENT_LONG_PRESS) {
        eraseAndRestart();
    }
    // SHORT_PRESS: no destructive action.
}

void resetButtonDelay(uint32_t ms) {
    const uint32_t startMs = millis();
    while ((uint32_t)(millis() - startMs) < ms) {
        resetButtonPoll();
        delay(10);
    }
}
