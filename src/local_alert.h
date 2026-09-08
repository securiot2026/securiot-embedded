#ifndef LOCAL_ALERT_H
#define LOCAL_ALERT_H

// Sets up the buzzer pin and the WS2812 status LED. Call once from
// setup(), before WiFi connects, leaves the LED in the "connecting" color.
void alertInit();

// Turns the status LED red and pulses the buzzer for a fixed duration.
// This is a delay()-based implementation, it briefly blocks the loop,
// acceptable at this MVP scale since it only fires on a real confirmed
// detection, not on every frame.
void triggerLocalAlert();

// Silences the buzzer and returns the LED to the idle color (green, WiFi
// connected). Called at the top of every response handling pass before the
// new frame's alert state is applied, so a false/null response clears a
// previous alert instead of latching it.
void clearLocalAlert();

// Status LED blue: WiFi connecting or reconnecting. Called from
// wifi_manager.cpp.
void indicateWifiConnecting();

// Status LED green: WiFi connected, no active alert (idle/normal
// operation). Called from wifi_manager.cpp once connected, and internally
// by clearLocalAlert().
void indicateWifiConnected();

// Status LED pulsing blue: captive portal active (AP/portal phase, or
// STA-connected-but-awaiting-/provision-extra phase). Distinct from the
// solid blue of indicateWifiConnecting(). Non-blocking (no delay()) — call
// every loop() iteration while provisioningActive() is true. Called from
// provisioning.cpp.
void indicatePortalActive();

// Status LED blinks red forever and never returns: fatal error (camera
// init failed). Called once from main.cpp's setup(), replaces the halt
// loop that used to follow a failed initCamera().
void indicateFatalError();

#endif // LOCAL_ALERT_H
