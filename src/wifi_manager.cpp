#include "wifi_manager.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "link_stability.h"
#include "local_alert.h"
#include "provisioning.h"

namespace {

const unsigned long kInitialConnectTimeoutMs = 20000;
const unsigned long kReconnectBackoffMinMs = 1000;
const unsigned long kReconnectBackoffMaxMs = 30000;
const int kMaxStoredCredentialAttempts = 3;
// Limits NVS re-reads while the portal is open. A missing namespace logs
// "nvs_open failed: NOT_FOUND" on every read, so polling every 10 ms floods
// the Serial log.
const unsigned long kPortalConfigRecheckIntervalMs = 500;

unsigned long g_reconnectBackoffMs = kReconnectBackoffMinMs;
unsigned long g_lastReconnectAttemptMs = 0;

// Whether the previous observation saw the STA link up. Used to apply power
// save off (and log it) on the rising edge only, once per link-up.
bool g_linkWasUp = false;

void trackLink(bool up) {
    if (linkShouldApplyPowerSave(g_linkWasUp, up)) {
        wifiDisablePowerSave();
    }
    g_linkWasUp = up;
}

// One attempt at WiFi.begin() with stored credentials, blocking up to
// kInitialConnectTimeoutMs. Returns true if it connected.
bool attemptStoredConnect(const String &ssid, const String &password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    indicateWifiConnecting();

    Serial.print("WiFi: connecting to ");
    Serial.println(ssid);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMs > kInitialConnectTimeoutMs) {
            return false;
        }
        delay(250);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("WiFi: connected, IP=");
    Serial.println(WiFi.localIP());
    indicateWifiConnected();
    trackLink(true);
    return true;
}

// Blocks until the stored config is valid, i.e. until the operator finished the
// portal. provisioningLoop() runs on every iteration (it feeds the portal and
// the delay yields), but NVS is only re-read once per
// kPortalConfigRecheckIntervalMs.
void waitForPortalConfig(String &ssid, String &password, String &edgeApiUrl, String &deviceKey) {
    unsigned long lastCheckMs = millis();
    bool configured = false;
    while (!configured) {
        provisioningLoop();
        delay(10);
        if (millis() - lastCheckMs >= kPortalConfigRecheckIntervalMs) {
            lastCheckMs = millis();
            configured = provisioningLoadConfig(ssid, password, edgeApiUrl, deviceKey);
        }
    }
}

} // namespace

void wifiDisablePowerSave() {
    // setSleep(false) keeps the Arduino cached sleep type coherent, so the core
    // re-applies it on every STA_START. It returns early without touching the
    // driver when the cached type is already none, hence the explicit call.
    WiFi.setSleep(false);
    const esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    char line[48];
    linkFormatPowerSaveLog(line, sizeof(line), (int)err);
    Serial.println(line);
}

void wifiConnect() {
    provisioningBegin();

    String ssid, password, edgeApiUrl, deviceKey;
    bool configured = provisioningLoadConfig(ssid, password, edgeApiUrl, deviceKey);

    // Defensive fallback: provisioningBegin() only starts the portal when
    // not yet configured, and a successful portal submission restarts the
    // device (see provisioning.cpp). So this loop only runs while the
    // operator is still filling out the form / the portal is validating -
    // by the time it exits, the caller is effectively on the post-restart
    // boot path anyway.
    if (!configured) {
        waitForPortalConfig(ssid, password, edgeApiUrl, deviceKey);
    }

    for (int attempt = 1; attempt <= kMaxStoredCredentialAttempts; attempt++) {
        if (attemptStoredConnect(ssid, password)) {
            g_reconnectBackoffMs = kReconnectBackoffMinMs;
            return;
        }
        Serial.printf("WiFi: attempt %d/%d timed out\n", attempt, kMaxStoredCredentialAttempts);
    }

    // 3 consecutive failures against stored (now-invalid) credentials:
    // reopen the portal instead of hanging on a dead connection forever,
    // per CONTEXT.md's locked decision.
    Serial.println("WiFi: 3 failed attempts against stored credentials, reopening portal");
    provisioningBegin(/*force=*/true);
    waitForPortalConfig(ssid, password, edgeApiUrl, deviceKey);
}

bool wifiEnsureConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        g_reconnectBackoffMs = kReconnectBackoffMinMs;
        trackLink(true);
        return true;
    }

    // Records the drop so the next link-up re-applies power save and logs once.
    trackLink(false);
    indicateWifiConnecting();

    unsigned long now = millis();
    if (now - g_lastReconnectAttemptMs < g_reconnectBackoffMs) {
        // Still inside the current backoff window, do not block, let loop()
        // skip this iteration's capture/send and try again next time.
        return false;
    }

    Serial.println("WiFi: not connected, attempting reconnect()");
    g_lastReconnectAttemptMs = now;
    WiFi.reconnect();

    g_reconnectBackoffMs = min(g_reconnectBackoffMs * 2, kReconnectBackoffMaxMs);

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        indicateWifiConnected();
        trackLink(true);
    }
    return connected;
}
