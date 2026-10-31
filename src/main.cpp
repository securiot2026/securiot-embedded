#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "camera_pins.h"
#include "camera_preview.h"
#include "local_alert.h"
#include "provisioning.h"
#include "secrets.h"
#include "servo_control.h"
#include "wifi_manager.h"

#ifndef CAMERA_PREVIEW_ONLY
#define CAMERA_PREVIEW_ONLY 0
#endif

// Frame size / quality for capture. VGA (640x480) keeps the multipart POST
// small enough for a WiFi upload every cycle while still giving the Edge
// API's YOLO pipeline enough resolution to detect a person reliably.
#define CAPTURE_FRAME_SIZE FRAMESIZE_VGA
#define CAPTURE_JPEG_QUALITY 12
#define CAPTURE_FRAME_WIDTH 640
#define CAPTURE_FRAME_HEIGHT 480

// Minimum time between capture/POST cycles.
#define CAPTURE_INTERVAL_MS 2000

namespace {

unsigned long g_lastCaptureMs = 0;

// Edge API base URL and device key, sourced from Preferences via
// provisioning.h (portal-managed, see provisioning.cpp) - re-read after a
// run of consecutive request failures in case the operator re-provisioned
// the device with a different URL.
String g_edgeApiBaseUrl;
String g_deviceKey;
// Device id (Cloud Device UUID) and Zone id, portal-provided and stored in
// NVS (compiled defaults only as fallback, see provisioning.cpp). Cached like
// the key so there is no NVS read per frame.
String g_deviceId;
String g_zoneId;
int g_consecutiveRequestFailures = 0;
const int kFailuresBeforeReResolve = 3;

bool initCamera() {
    camera_config_t config = {};
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;

    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;

    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = CAPTURE_FRAME_SIZE;
    config.jpeg_quality = CAPTURE_JPEG_QUALITY;
    config.fb_count = 1;

    // PSRAM is required for stable JPEG capture at VGA on the AI-Thinker
    // ESP32-CAM, see docs/PINOUT.md.
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
#if CAMERA_PREVIEW_ONLY
    // Two JPEG buffers allow capture and transmission to overlap. Discard
    // stale queued frames instead of adding latency when Wi-Fi slows down.
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
#endif

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    return true;
}

// Builds a multipart/form-data body matching the Edge API's documented
// /frames contract exactly: device_id, zone_id, frame_width, frame_height
// text fields plus a "frame" file field carrying the JPEG bytes.
String buildMultipartBody(const String &boundary, camera_fb_t *fb,
                           String &outHead, String &outTail) {
    outHead = "";
    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
    outHead += g_deviceId + "\r\n";

    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"zone_id\"\r\n\r\n";
    outHead += g_zoneId + "\r\n";

    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"frame_width\"\r\n\r\n";
    outHead += String(CAPTURE_FRAME_WIDTH) + "\r\n";

    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"frame_height\"\r\n\r\n";
    outHead += String(CAPTURE_FRAME_HEIGHT) + "\r\n";

    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"frame\"; filename=\"frame.jpg\"\r\n";
    outHead += "Content-Type: image/jpeg\r\n\r\n";

    outTail = "\r\n--" + boundary + "--\r\n";

    return outHead;
}

void handleFramesResponse(HTTPClient &http) {
    String payload = http.getString();

    JsonDocument doc;
    DeserializationError parseErr = deserializeJson(doc, payload);
    if (parseErr) {
        Serial.print("JSON parse failed: ");
        Serial.println(parseErr.f_str());
        return;
    }

    float panDelta = doc["pan_delta"].isNull() ? NAN : doc["pan_delta"].as<float>();
    float tiltDelta = doc["tilt_delta"].isNull() ? NAN : doc["tilt_delta"].as<float>();
    const char *doorAction = doc["door_action"].isNull() ? nullptr : doc["door_action"].as<const char *>();
    bool alert = doc["alert"] | false;

    Serial.print("pan_delta=");
    Serial.print(isnan(panDelta) ? "null" : String(panDelta));
    Serial.print(" tilt_delta=");
    Serial.print(isnan(tiltDelta) ? "null" : String(tiltDelta));
    Serial.print(" door_action=");
    Serial.print(doorAction ? doorAction : "null");
    Serial.print(" alert=");
    Serial.println(alert ? "true" : "false");

    if (!isnan(panDelta) && !isnan(tiltDelta)) {
        applyPanTilt(panDelta, tiltDelta);
    }
    if (doorAction && strcmp(doorAction, "lock") == 0) {
        triggerDoorLock();
    }
    if (alert) {
        triggerLocalAlert();
    } else {
        clearLocalAlert();
    }

    // Manual command (Phase 7): delivered on this same /frames response,
    // independent of detection state. The Edge API already resolved
    // precedence against the same-cycle automatic pan/tilt tracking delta
    // (it omits pan_delta/tilt_delta above when a manual pan_tilt command
    // is present) — the firmware only needs to dispatch whichever value it
    // received.
    JsonVariant manualCommand = doc["manual_command"];
    if (!manualCommand.isNull()) {
        const char *actuator = manualCommand["actuator"] | "";
        JsonVariant action = manualCommand["action"];

        Serial.print("manual_command actuator=");
        Serial.println(actuator);

        if (strcmp(actuator, "pan_tilt") == 0) {
            float manualPan = action["pan_delta"].isNull() ? NAN : action["pan_delta"].as<float>();
            float manualTilt = action["tilt_delta"].isNull() ? NAN : action["tilt_delta"].as<float>();
            if (!isnan(manualPan) && !isnan(manualTilt)) {
                applyPanTilt(manualPan, manualTilt);
            }
        } else if (strcmp(actuator, "light") == 0) {
            setManualLight(action["on"] | false);
        } else if (strcmp(actuator, "buzzer") == 0) {
            setManualBuzzer(action["on"] | false);
        }
    }
}

void captureAndSendFrame() {
    if (g_edgeApiBaseUrl.isEmpty()) {
        Serial.println("Edge API base URL not resolved, skipping this cycle");
        g_edgeApiBaseUrl = provisioningGetEdgeApiUrl();
        return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed, skipping this cycle");
        return;
    }

    String boundary = "SecurIoTFrameBoundary";
    String head;
    String tail;
    buildMultipartBody(boundary, fb, head, tail);

    size_t totalLen = head.length() + fb->len + tail.length();
    uint8_t *body = (uint8_t *)malloc(totalLen);
    if (!body) {
        Serial.println("Failed to allocate multipart body buffer, skipping this cycle");
        esp_camera_fb_return(fb);
        return;
    }

    size_t offset = 0;
    memcpy(body + offset, head.c_str(), head.length());
    offset += head.length();
    memcpy(body + offset, fb->buf, fb->len);
    offset += fb->len;
    memcpy(body + offset, tail.c_str(), tail.length());

    HTTPClient http;
    String url = g_edgeApiBaseUrl + "/frames";
    http.begin(url);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.addHeader("X-Device-Key", g_deviceKey);

    int httpCode = http.POST(body, totalLen);

    // The framebuffer must stay valid for the whole request, only release it
    // once the POST has completed.
    esp_camera_fb_return(fb);
    free(body);

    if (httpCode == HTTP_CODE_OK) {
        g_consecutiveRequestFailures = 0;
        handleFramesResponse(http);
    } else {
        if (httpCode > 0) {
            Serial.printf("Edge API returned HTTP %d\n", httpCode);
        } else {
            Serial.printf("HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
        }

        // A wrong/stale resolved IP (Edge API host restarted with a new
        // DHCP lease, for example) looks like a run of failures here, not
        // like a single clean error, so only re-resolve after a few in a
        // row, not on every single hiccup.
        g_consecutiveRequestFailures++;
        if (g_consecutiveRequestFailures >= kFailuresBeforeReResolve) {
            Serial.println("Edge API: too many consecutive failures, re-resolving base URL");
            g_edgeApiBaseUrl = provisioningGetEdgeApiUrl();
            g_deviceId = provisioningGetDeviceId();
            g_zoneId = provisioningGetZoneId();
            g_consecutiveRequestFailures = 0;
        }
    }

    http.end();
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("SecurIoT ESP32-CAM firmware starting");

#if !CAMERA_PREVIEW_ONLY
    servosInit();
#endif
    alertInit();

    if (!initCamera()) {
        Serial.println("FATAL: camera init failed, halting");
        indicateFatalError(); // blinks red forever, never returns
    }

    wifiConnect();

#if CAMERA_PREVIEW_ONLY
    cameraPreviewBegin();
#else
    g_edgeApiBaseUrl = provisioningGetEdgeApiUrl();
    // Cached once here (not read from Preferences/NVS on every frame) to
    // avoid an NVS read on every HTTP request's latency path.
    g_deviceKey = provisioningGetDeviceKey();
    g_deviceId = provisioningGetDeviceId();
    g_zoneId = provisioningGetZoneId();
    // Ids only, never the device key.
    Serial.printf("Identity: device_id=%s zone_id=%s\n", g_deviceId.c_str(), g_zoneId.c_str());
#endif
}

void loop() {
    // Must run every iteration while the AP/portal or /provision-extra
    // window is active (RESEARCH.md Pattern 2); a fast no-op otherwise.
    provisioningLoop();
    if (provisioningActive()) {
        // Operator is still configuring the device - no valid WiFi/Edge
        // connection to use yet, skip the capture/send cycle entirely.
        return;
    }

    if (!wifiEnsureConnected()) {
        // Not connected yet, backoff is handled inside wifi_manager, do not
        // block here, just skip this cycle and retry on the next loop().
        delay(200);
        return;
    }

    // Expire manual holds and rule effects (wrap-safe) and repaint the LED and
    // buzzer if the composed state changed; cheap no-op when nothing changed.
    alertTick();

#if CAMERA_PREVIEW_ONLY
    // HTTP servers run on their own tasks; keep Wi-Fi recovery responsive.
    delay(20);
#else
    unsigned long now = millis();
    if (now - g_lastCaptureMs < CAPTURE_INTERVAL_MS) {
        delay(50);
        return;
    }
    g_lastCaptureMs = now;

    captureAndSendFrame();
#endif
}
