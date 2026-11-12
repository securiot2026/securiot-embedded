#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// Blocking initial WiFi connect. Reads stored credentials from Preferences
// via provisioning.h; if none exist (or 3 consecutive 20s connect attempts
// against stored credentials fail), starts the captive portal via
// provisioningBegin() and blocks until the operator provisions the device
// (which restarts it) or, on the defensive fallback path, until
// provisioningLoadConfig() reports a valid config. Meant to be called once
// from setup().
void wifiConnect();

// Non-blocking connectivity check, call at the top of every loop() iteration.
// Returns true if WiFi is currently connected. If it is not, attempts a
// reconnect with exponential backoff (capped) and returns false for this
// iteration without blocking the caller for the full backoff duration.
// PITFALLS.md Pitfall 2 requires this check on every loop iteration, not
// only at boot, so a silent WiFi drop recovers without a manual power-cycle.
// After about 2 minutes of continuous loss (LINK_LOSS_PORTAL_TIMEOUT_MS) the
// setup portal is reopened without blocking and the device stays in
// provisioning until the operator finishes it (the board restarts), resets it
// with the button or power-cycles it.
bool wifiEnsureConnected();

// Switches WiFi power save off (WiFi.setSleep(false) plus an explicit
// esp_wifi_set_ps(WIFI_PS_NONE)) and prints one `WiFi: power save disabled`
// line. Idempotent and safe from any task, call it only while STA is up.
void wifiDisablePowerSave();

#endif // WIFI_MANAGER_H
