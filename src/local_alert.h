#ifndef LOCAL_ALERT_H
#define LOCAL_ALERT_H

#include <stdint.h>

#include "button_state.h"

// Sets up the buzzer pin and the WS2812 status LED. Call once from
// setup(), before WiFi connects, leaves the LED in the "connecting" color.
void alertInit();

// Turns the status LED red and pulses the buzzer for a fixed duration.
// This is a delay()-based implementation, it briefly blocks the loop,
// acceptable at this MVP scale since it only fires on a real confirmed
// detection, not on every frame.
void triggerLocalAlert();

// Clears ONLY the generic detection alert flag (red LED). Called at the top
// of every response handling pass so a false/null response does not latch a
// previous alert. It deliberately does NOT cancel an active manual light or
// buzzer hold, nor a rule effect: those end on an explicit off command or on
// their own timer (audit G4, D-05). LED and buzzer are recomposed afterwards.
void clearLocalAlert();

// Manual "light" actuator. `on == true` holds the status LED in `colorName`
// (named color or #RRGGBB, default white) until an explicit `on == false` or
// `ttlMs` elapses (0 = default hold of 120 s). Beats rule effects and the
// generic alert in LED priority. Does not touch the buzzer and never blocks.
void setManualLight(bool on, const char *colorName = nullptr, uint32_t ttlMs = 0);

// Manual "buzzer" actuator. `on == true` holds the buzzer tone until an
// explicit `on == false` or `ttlMs` elapses (0 = default 120 s). Non-blocking
// and independent of the LED; survives clearLocalAlert() and the blocking
// alert pulse.
void setManualBuzzer(bool on, uint32_t ttlMs = 0);

// Rule effects (Web rule actions delivered as rule_command, D-03): light
// shows `colorName` (default red) and the buzzer sounds for a fixed 5 s, then
// both expire on their own. `on == false` ends the effect early. Lower LED
// priority than a manual light hold.
void applyRuleLight(bool on, const char *colorName = nullptr);
void applyRuleBuzzer(bool on);

// Call every loop() iteration once Wi-Fi is up: expires holds and effects
// (wrap-safe) and repaints the LED/buzzer when the composed state changed. It
// also animates the status patterns (blink, pulse), so it is safe to call
// inside blocking waits.
void alertTick();

// Status LED slow blue blink: WiFi connecting or reconnecting. Called from
// wifi_manager.cpp.
void indicateWifiConnecting();

// Status LED green: WiFi connected, no active alert (idle/normal
// operation). Called from wifi_manager.cpp once connected, and internally
// by clearLocalAlert().
void indicateWifiConnected();

// Status LED for the captive portal: pulsing cyan while no phone joined the
// SoftAP, solid cyan once a phone joined, pulsing yellow once the WiFi
// credentials were accepted and /provision-extra is pending. Non-blocking (no
// delay()): must be refreshed on every loop() iteration while the portal is
// active, because the pulse only animates when this is called often. Called
// from provisioning.cpp.
void indicatePortalActive(bool credentialsAccepted, int apClients);

// Long-press overlay of the reset button: magenta blink that speeds up while
// the button is held, fast white blink once the reset is confirmed. Pass
// BUTTON_PHASE_IDLE or BUTTON_PHASE_PRESSED to show the normal state again.
// Called from reset_button.cpp on every poll.
void indicateButtonProgress(ButtonPhase phase, uint32_t heldMs);

// Status LED blinks red forever and never returns: fatal error (camera
// init failed). Called once from main.cpp's setup(), replaces the halt
// loop that used to follow a failed initCamera().
void indicateFatalError();

#endif // LOCAL_ALERT_H
