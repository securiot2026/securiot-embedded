#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <Arduino.h>

// Field-configurable WiFi/Edge-API provisioning via a two-part captive
// portal flow (see docs/BRINGUP.md and .planning/phases/06-*/06-RESEARCH.md
// for the design rationale):
//   1. MycilaESPConnect's own AP + portal collects SSID/password only (its
//      Config struct has no fields for anything else).
//   2. Once WiFi connects (STA), a custom /provision-extra HTTP form on the
//      same shared AsyncWebServer collects the Edge API URL, device key,
//      Device id and Zone id.
// All six values persist in NVS via Preferences (namespace "securiot")
// only after the submitted Edge API URL passes a reachability check.

// Starts the provisioning flow if the stored config is missing/invalid:
// registers the /provision-extra routes on the shared AsyncWebServer and
// calls espConnect.begin() (forces the AP+portal, no timeout). If a valid
// stored config already exists AND force is false, returns immediately
// without starting anything (06-RESEARCH.md Anti-Patterns: calling
// espConnect.begin() unconditionally on every boot defeats the
// "Preferences first" decision). Pass force=true to start the portal
// regardless of the stored "configured" flag - used when 3 consecutive
// connect attempts against stored (now-stale) credentials have failed and
// the portal must reopen even though Preferences still says "configured".
// Call once from wifiConnect().
void provisioningBegin(bool force = false);

// True whenever the AP/portal is up (AP_STARTED/PORTAL_STARTED), OR WiFi
// has connected (STA) but /provision-extra has not yet been successfully
// submitted. This is the single source of truth main.cpp's loop() uses to
// skip capture/send for the entire provisioning window.
bool provisioningActive();

// Drives the portal/AP state machine and the custom /provision-extra
// server. Must be called every loop() iteration while provisioningActive()
// is true (and is harmless, a fast no-op, when it is not).
void provisioningLoop();

// Reads all 4 fields from Preferences (read-only). Returns the stored
// "configured" flag. Does not start the portal — call provisioningBegin()
// separately if this returns false.
bool provisioningLoadConfig(String &ssid, String &password,
                             String &edgeApiUrl, String &deviceKey);

// Thin wrappers over provisioningLoadConfig() for callers that only need
// one field (main.cpp's setup(), captureAndSendFrame()).
String provisioningGetEdgeApiUrl();
String provisioningGetDeviceKey();

// Device id (Cloud Device UUID) and Zone id collected by the portal's
// /provision-extra form. Return the NVS value when the "configured" flag is
// true and the stored value is a valid identity token (see
// device_identity.h); otherwise the compiled fallback from secrets.h, so a
// device provisioned before these fields existed keeps working.
String provisioningGetDeviceId();
String provisioningGetZoneId();

#endif // PROVISIONING_H
