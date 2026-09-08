#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// Blocking initial WiFi connect. Loops with a timeout and retries until
// connected, meant to be called once from setup().
void wifiConnect();

// Non-blocking connectivity check, call at the top of every loop() iteration.
// Returns true if WiFi is currently connected. If it is not, attempts a
// reconnect with exponential backoff (capped) and returns false for this
// iteration without blocking the caller for the full backoff duration.
// PITFALLS.md Pitfall 2 requires this check on every loop iteration, not
// only at boot, so a silent WiFi drop recovers without a manual power-cycle.
bool wifiEnsureConnected();

#endif // WIFI_MANAGER_H
