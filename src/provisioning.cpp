#include "provisioning.h"

#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <MycilaESPConnect.h>
#include <Preferences.h>
#include <WiFi.h>

#include "device_identity.h"
#include "local_alert.h"
#include "portal_prefill.h"
#include "secrets.h"

// Compiled fallbacks, used only when NVS holds no valid portal-provided
// identity (device provisioned before Phase 12). The legacy placeholders are
// valid identity tokens, so a local secrets.h lacking these macros still
// builds.
#ifndef DEVICE_ID
#define DEVICE_ID "esp32cam-01"
#endif
#ifndef ZONE_ID
#define ZONE_ID "zone-01"
#endif

namespace {

// Single shared AsyncWebServer instance for the whole firmware. Any future
// phase adding HTTP routes must reuse this same instance (see
// 06-RESEARCH.md Pitfall 2) rather than constructing a second server on
// port 80.
AsyncWebServer server(80);
Mycila::ESPConnect espConnect(server);
Preferences prefs;

const char *kPrefsNamespace = "securiot";
const unsigned long kEdgeApiPingTimeoutMs = 5000;

// Set true when MycilaESPConnect validates WiFi credentials
// (PORTAL_COMPLETE), false again once /provision-extra has been
// successfully submitted (config saved, device about to restart) or on a
// fresh boot before the portal even starts. This is the second half of
// provisioningActive()'s "still provisioning" window, distinct from the
// AP/portal phase which MycilaESPConnect's own state machine tracks.
bool g_credentialsValidated = false;

// Set true in the same instant g_credentialsValidated becomes true. Once
// set, provisioningLoop() stops calling espConnect.loop() for the rest of
// this boot. This is deliberate: MycilaESPConnect's own loop(), on the
// very next call after PORTAL_COMPLETE, tears down the SoftAP
// (WiFi.softAPdisconnect()) to move to a plain STA-only connection - which
// would kick the operator's phone off SecurIoT-Setup-XXXX before they
// ever reach /provision-extra. Pausing the library's loop() keeps AP+STA
// both alive (WiFi is already connected STA-side from the credential
// test itself - MycilaESPConnect runs AP+STA concurrently while testing),
// so the phone never has to leave the AP network at all. The
// AsyncWebServer and DNS server keep serving regardless (both are
// event-driven / task-backed, not loop()-driven) - see 06-RESEARCH.md
// (this phase's second research pass) for the traced library source.
bool g_espConnectLoopPaused = false;

// Holds SSID/password captured from MycilaESPConnect's Config once
// credentials validate, until the operator submits /provision-extra and
// the full 4-field config can be saved together.
String g_capturedSsid;
String g_capturedPassword;

// Setup-link prefill (Setup QR). RAM only, never written to Serial, filled from
// the query args of GET /provision-extra. It survives until reboot so the later
// post-WiFi visit to /provision-extra (the "/" redirect drops the query string)
// still prefills the form.
PortalPrefill g_prefill;

bool started = false;

// Fixed SoftAP gateway IP MycilaESPConnect configures for every captive
// portal it starts (WiFi.softAPConfig(IPAddress(4,3,2,1), ...) in the
// library itself) - a known, unchanging address the operator can go to
// directly, instead of hunting for a dynamic IP off Serial.
const char *kApGatewayUrl = "http://4.3.2.1/provision-extra";

// SecurIoT-Setup-<last 4 hex of MAC>, per CONTEXT.md - avoids AP name
// collisions between multiple devices in the field.
// [ASSUMED: WiFi.macAddress(uint8_t*) signature stable across pioarduino
// core 2.x/3.x, per 06-RESEARCH.md Pattern 3 / Assumption A1]
String apSsidWithMacSuffix() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[5];
    snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
    return "SecurIoT-Setup-" + String(buf);
}

// The form page is assembled from static PROGMEM segments plus, for each of
// the four inputs, a value attribute built by valueAttribute() (escaped, only
// inside double quotes). No value is ever placed in script, element text or an
// unquoted context. On ESP32 PROGMEM data is directly readable, so plain
// String concatenation of these pointers works.
const char kFormHead[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>SecurIoT device setup</title></head><body>
<h1>SecurIoT device setup</h1>
<p>WiFi credentials accepted. Stay connected to the SecurIoT-Setup network
and enter the Edge API URL, the device key, the Device id and the Zone id below
to finish setup. The Device id and the Zone id must match the Device registered
in the Web App, otherwise rules, manual control and the live feed will not work.</p>
<form method="POST" action="/provision-extra">
  <label>Edge API URL (e.g. http://192.168.0.22:5000)<br>
    <input type="text" name="edgeApiUrl" required)HTML";

const char kFormAfterEdge[] PROGMEM = R"HTML(></label><br><br>
  <label>Device key<br>
    <input type="text" name="deviceKey" required)HTML";

const char kFormAfterKey[] PROGMEM = R"HTML(></label><br><br>
  <label>Device id (copy it from the Web App)<br>
    <input type="text" name="deviceId" required maxlength="64"
           pattern="[A-Za-z0-9._\-]{1,64}" autocomplete="off")HTML";

const char kFormAfterDeviceId[] PROGMEM = R"HTML(></label><br><br>
  <label>Zone id (copy it from the Web App)<br>
    <input type="text" name="zoneId" required maxlength="64"
           pattern="[A-Za-z0-9._\-]{1,64}" autocomplete="off")HTML";

// Static script, no interpolation: strips the query string (which may carry the
// device key) from the address bar once the page has loaded.
const char kFormTail[] PROGMEM = R"HTML(></label><br><br>
  <input type="submit" value="Save and connect">
</form>
<script>history.replaceState(null,"",location.pathname);</script>
</body></html>
)HTML";

String buildProvisionExtraFormHtml() {
    String html;
    html.reserve(1800);
    html += kFormHead;
    html += valueAttribute(g_prefill.edgeApiUrl).c_str();
    html += kFormAfterEdge;
    html += valueAttribute(g_prefill.deviceKey).c_str();
    html += kFormAfterKey;
    html += valueAttribute(g_prefill.deviceId).c_str();
    html += kFormAfterDeviceId;
    html += valueAttribute(g_prefill.zoneId).c_str();
    html += kFormTail;
    return html;
}

const char kProvisionExtraRetryHtml[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>SecurIoT device setup - retry</title></head><body>
<h1>Could not reach the Edge API</h1>
<p>The Edge API URL you entered did not respond. Check the URL and try again.</p>
<p><a href="/provision-extra">Back to setup form</a></p>
</body></html>
)HTML";

const char kProvisionExtraInvalidIdentityHtml[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>SecurIoT device setup - invalid identity</title></head><body>
<h1>Invalid Device id or Zone id</h1>
<p>Both values are required. Use only letters, digits, dot, underscore and
hyphen, up to 64 characters. Nothing was saved.</p>
<p><a href="/provision-extra">Back to setup form</a></p>
</body></html>
)HTML";

const char kProvisionExtraSavedHtml[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<title>SecurIoT device setup - saved</title></head><body>
<h1>Saved. Restarting...</h1>
</body></html>
)HTML";

// Plain GET against the submitted base URL (no assumed /health path, per
// 06-RESEARCH.md Assumption A2 — the Edge API's exact health route is
// unconfirmed). Any HTTP response code greater than 0 counts as
// "reachable"; this only proves the URL answers HTTP, not that it is the
// legitimate Edge API (see 06-01-PLAN.md threat T-06-02, accepted).
bool pingEdgeApi(const String &edgeApiUrl) {
    HTTPClient http;
    http.setConnectTimeout(kEdgeApiPingTimeoutMs);
    http.begin(edgeApiUrl);
    int code = http.GET();
    http.end();
    return code > 0;
}

void saveConfig(const String &ssid, const String &password,
                 const String &edgeApiUrl, const String &deviceKey,
                 const String &deviceId, const String &zoneId) {
    prefs.begin(kPrefsNamespace, /*readOnly=*/false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.putString("edgeApiUrl", edgeApiUrl);
    prefs.putString("deviceKey", deviceKey);
    prefs.putString("deviceId", deviceId);
    prefs.putString("zoneId", zoneId);
    prefs.putBool("configured", true);
    prefs.end();
}

// Reads one setup-link query arg; NULL-equivalent (empty String) when absent.
// The returned String is only ever fed to sanitizePortalPrefill, never logged.
bool readQueryArg(AsyncWebServerRequest *request, const char *name, String &out) {
    if (!request->hasParam(name)) {
        return false;
    }
    out = request->getParam(name)->value();
    return true;
}

void handleProvisionExtraGet(AsyncWebServerRequest *request) {
    String edgeApiUrl, deviceKey, deviceId, zoneId;
    const bool hasEdge = readQueryArg(request, "edgeApiUrl", edgeApiUrl);
    const bool hasKey = readQueryArg(request, "deviceKey", deviceKey);
    const bool hasId = readQueryArg(request, "deviceId", deviceId);
    const bool hasZone = readQueryArg(request, "zoneId", zoneId);
    const bool hasArgs = hasEdge || hasKey || hasId || hasZone;

    if (hasArgs) {
        // Replaces the previous stash wholesale, so an invalid value leaves
        // its field empty. Only a count is logged, never a value.
        g_prefill = sanitizePortalPrefill(hasEdge ? edgeApiUrl.c_str() : nullptr,
                                          hasKey ? deviceKey.c_str() : nullptr,
                                          hasId ? deviceId.c_str() : nullptr,
                                          hasZone ? zoneId.c_str() : nullptr);
        const int acceptedFields = g_prefill.count();
        Serial.printf("Provisioning: setup link accepted %d of 4 fields\n", acceptedFields);

        if (!g_credentialsValidated) {
            // The WiFi step is not done yet: the POST would have no captured
            // SSID. Send the operator to the library's WiFi form first; after
            // PORTAL_COMPLETE the existing "/" redirect lands here again and
            // the stashed values prefill the form.
            request->redirect("/");
            return;
        }
    }

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", buildProvisionExtraFormHtml());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// Redirects "/" to /provision-extra once credentials are validated. This
// handler is registered BEFORE espConnect.begin() (so it lands first in
// AsyncWebServer's handler list), but its filter only returns true once
// g_credentialsValidated - during the WiFi-form phase it's skipped and
// falls through to MycilaESPConnect's own "/" handler (its filter checks
// PORTAL_STARTED). Once credentials validate, this one wins instead. This
// is what makes the OS's captive-portal detection chain straight into
// /provision-extra: the OS periodically re-probes connectivity endpoints
// (/generate_204, /hotspot-detect.html, etc.), MycilaESPConnect always
// redirects those to "/" regardless of state, and "/" now points here -
// no IP-hunting required, the operator's phone never leaves the AP.
void handleRootRedirect(AsyncWebServerRequest *request) {
    request->redirect("/provision-extra");
}

void handleProvisionExtraPost(AsyncWebServerRequest *request) {
    if (!request->hasParam("edgeApiUrl", true) || !request->hasParam("deviceKey", true)) {
        request->send(400, "text/html", kProvisionExtraRetryHtml);
        return;
    }

    // Validate identity BEFORE pinging the Edge API: a bad value must never
    // reach NVS or, later, the multipart body. The response does not echo
    // the submitted values.
    if (!request->hasParam("deviceId", true) || !request->hasParam("zoneId", true)) {
        request->send(400, "text/html", kProvisionExtraInvalidIdentityHtml);
        return;
    }

    String edgeApiUrl = request->getParam("edgeApiUrl", true)->value();
    String deviceKey = request->getParam("deviceKey", true)->value();
    String deviceId = request->getParam("deviceId", true)->value();
    String zoneId = request->getParam("zoneId", true)->value();

    if (!isValidIdentityToken(deviceId.c_str()) || !isValidIdentityToken(zoneId.c_str())) {
        Serial.println("Provisioning: invalid Device id or Zone id, not saving");
        request->send(400, "text/html", kProvisionExtraInvalidIdentityHtml);
        return;
    }

    if (!pingEdgeApi(edgeApiUrl)) {
        Serial.println("Provisioning: Edge API ping failed, not saving, re-serving retry form");
        request->send(200, "text/html", kProvisionExtraRetryHtml);
        return;
    }

    saveConfig(g_capturedSsid, g_capturedPassword, edgeApiUrl, deviceKey, deviceId, zoneId);
    g_credentialsValidated = false;
    Serial.println("Provisioning: config saved, restarting");
    request->send(200, "text/html", kProvisionExtraSavedHtml);
    delay(200); // let the response flush before restarting
    ESP.restart();
}

} // namespace

void provisioningBegin(bool force) {
    if (started) {
        // Already running (e.g. reopened after 3 failed stored-credential
        // attempts) - espConnect keeps serving via provisioningLoop(), no
        // need to re-register routes or call begin() again.
        return;
    }

    if (!force) {
        // Calling espConnect.begin() unconditionally on every boot defeats
        // the "Preferences first" decision and re-triggers the portal even
        // when saved credentials are valid (06-RESEARCH.md Anti-Patterns).
        // Only start the AP/portal when there is no valid stored config
        // yet, unless the caller forces a reopen (stale credentials).
        String ssid, password, edgeApiUrl, deviceKey;
        if (provisioningLoadConfig(ssid, password, edgeApiUrl, deviceKey)) {
            return;
        }
    }

    started = true;

    server.on("/provision-extra", HTTP_GET, handleProvisionExtraGet);
    server.on("/provision-extra", HTTP_POST, handleProvisionExtraPost);

    // Registered before espConnect.begin() so it lands first in
    // AsyncWebServer's handler list, ahead of MycilaESPConnect's own "/"
    // handler (see handleRootRedirect()'s comment above).
    AsyncCallbackWebHandler *rootRedirectHandler =
        &server.on("/", HTTP_GET, handleRootRedirect);
    rootRedirectHandler->setFilter(
        [](__unused AsyncWebServerRequest *request) { return g_credentialsValidated; });

    espConnect.listen([](Mycila::ESPConnect::State previous, Mycila::ESPConnect::State current) {
        (void)previous;
        switch (current) {
            case Mycila::ESPConnect::State::AP_STARTED:
            case Mycila::ESPConnect::State::PORTAL_STARTED:
                indicatePortalActive();
                break;
            case Mycila::ESPConnect::State::PORTAL_COMPLETE: {
                Mycila::ESPConnect::Config cfg = espConnect.getConfig();
                // Config.wifiSSID/wifiPassword are std::string, not
                // Arduino String - explicit c_str() conversion required.
                g_capturedSsid = String(cfg.wifiSSID.c_str());
                g_capturedPassword = String(cfg.wifiPassword.c_str());
                g_credentialsValidated = true;
                // Deliberate: see g_espConnectLoopPaused's declaration
                // comment - this stops espConnect.loop() from tearing
                // down the SoftAP on the very next call.
                g_espConnectLoopPaused = true;
                Serial.println("Provisioning: WiFi credentials accepted, WiFi connected (STA) "
                                "while SoftAP stays up");
                Serial.print("Provisioning: browse to ");
                Serial.print(kApGatewayUrl);
                Serial.println(" (stay connected to the SecurIoT-Setup network - most phones "
                                "open this automatically)");
                break;
            }
            default:
                break;
        }
    });

    espConnect.setBlocking(false);
    // Without this, MycilaESPConnect restarts the device itself right after
    // WiFi credentials validate (PORTAL_COMPLETE), before the operator ever
    // reaches /provision-extra - saveConfig() would never have run, so
    // "configured" stays false and the device loops back into AP/portal
    // mode from scratch. This project owns the restart manually, only
    // after a successful /provision-extra submission (see
    // handleProvisionExtraPost). Also see g_espConnectLoopPaused's comment:
    // even with setAutoRestart(false), the library's own loop() still tears
    // down the SoftAP on PORTAL_COMPLETE unless we stop calling loop().
    // [VERIFIED: mathieucarbou/MycilaESPConnect/src/MycilaESPConnect.h:220,
    // default _autoRestart = true]
    espConnect.setAutoRestart(false);

    // 3-string overload (hostname, AP SSID, AP password): empty password
    // means an open AP, per CONTEXT.md's locked decision (threat T-06-01,
    // accepted). No captive-portal timeout is MycilaESPConnect's own
    // default when no SSID is known yet, matching CONTEXT.md's "no
    // timeout, stays open indefinitely" decision.
    espConnect.begin("securiot-cam", apSsidWithMacSuffix().c_str(), "");
}

bool provisioningActive() {
    if (g_credentialsValidated) {
        // Still waiting on /provision-extra (or its Edge API ping retry)
        // - espConnect.loop() is paused so its own state stays frozen at
        // PORTAL_COMPLETE, this flag is the source of truth from here on.
        return true;
    }
    Mycila::ESPConnect::State state = espConnect.getState();
    return state == Mycila::ESPConnect::State::AP_STARTED ||
           state == Mycila::ESPConnect::State::PORTAL_STARTED;
}

void provisioningLoop() {
    if (g_espConnectLoopPaused) {
        // AsyncWebServer and the DNS server are both event-driven, not
        // loop()-driven - they keep serving /provision-extra and
        // redirecting captive-portal probes to "/" without this call.
        return;
    }
    espConnect.loop();
}

bool provisioningLoadConfig(String &ssid, String &password, String &edgeApiUrl,
                             String &deviceKey) {
    prefs.begin(kPrefsNamespace, /*readOnly=*/true);
    // Preferences has no concept of "key does not exist" distinct from
    // "key exists with the default value" - always gate on the explicit
    // "configured" boolean, never on whether the string fields happen to
    // be non-empty (06-RESEARCH.md Pitfall 3).
    bool configured = prefs.getBool("configured", false);
    if (configured) {
        ssid = prefs.getString("ssid", "");
        password = prefs.getString("password", "");
        edgeApiUrl = prefs.getString("edgeApiUrl", "");
        deviceKey = prefs.getString("deviceKey", "");
    }
    prefs.end();
    return configured && !ssid.isEmpty();
}

String provisioningGetEdgeApiUrl() {
    String ssid, password, edgeApiUrl, deviceKey;
    provisioningLoadConfig(ssid, password, edgeApiUrl, deviceKey);
    return edgeApiUrl;
}

String provisioningGetDeviceKey() {
    String ssid, password, edgeApiUrl, deviceKey;
    provisioningLoadConfig(ssid, password, edgeApiUrl, deviceKey);
    return deviceKey;
}

namespace {

// Reads one identity key from NVS, gated on the "configured" boolean (never
// on string emptiness, Pitfall 3), and resolves it against the compiled
// fallback. A bad stored value falls back instead of breaking frame posts.
String loadIdentity(const char *key, const char *fallback) {
    prefs.begin(kPrefsNamespace, /*readOnly=*/true);
    bool configured = prefs.getBool("configured", false);
    String stored;
    if (configured) {
        stored = prefs.getString(key, "");
    }
    prefs.end();
    return String(resolveIdentity(configured ? stored.c_str() : nullptr, fallback));
}

} // namespace

String provisioningGetDeviceId() {
    return loadIdentity("deviceId", DEVICE_ID);
}

String provisioningGetZoneId() {
    return loadIdentity("zoneId", ZONE_ID);
}
