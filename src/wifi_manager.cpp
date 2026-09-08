#include "wifi_manager.h"

#include <WiFi.h>

#include "local_alert.h"
#include "secrets.h"

namespace {

const unsigned long kInitialConnectTimeoutMs = 20000;
const unsigned long kReconnectBackoffMinMs = 1000;
const unsigned long kReconnectBackoffMaxMs = 30000;

unsigned long g_reconnectBackoffMs = kReconnectBackoffMinMs;
unsigned long g_lastReconnectAttemptMs = 0;

} // namespace

void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    indicateWifiConnecting();

    Serial.print("WiFi: connecting to ");
    Serial.println(WIFI_SSID);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startMs > kInitialConnectTimeoutMs) {
            Serial.println("WiFi: initial connect timed out, retrying begin()");
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            startMs = millis();
        }
        delay(250);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("WiFi: connected, IP=");
    Serial.println(WiFi.localIP());

    indicateWifiConnected();
    g_reconnectBackoffMs = kReconnectBackoffMinMs;
}

bool wifiEnsureConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        g_reconnectBackoffMs = kReconnectBackoffMinMs;
        return true;
    }

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
    }
    return connected;
}
