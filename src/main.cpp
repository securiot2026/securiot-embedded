#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "camera_pins.h"
#include "local_alert.h"
#include "secrets.h"
#include "servo_control.h"
#include "wifi_manager.h"

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
    outHead += String(DEVICE_ID) + "\r\n";

    outHead += "--" + boundary + "\r\n";
    outHead += "Content-Disposition: form-data; name=\"zone_id\"\r\n\r\n";
    outHead += String(ZONE_ID) + "\r\n";

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
}

void captureAndSendFrame() {
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
    String url = String(EDGE_API_URL) + "/frames";
    http.begin(url);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.addHeader("X-Device-Key", DEVICE_KEY);

    int httpCode = http.POST(body, totalLen);

    // The framebuffer must stay valid for the whole request, only release it
    // once the POST has completed.
    esp_camera_fb_return(fb);
    free(body);

    if (httpCode == HTTP_CODE_OK) {
        handleFramesResponse(http);
    } else if (httpCode > 0) {
        Serial.printf("Edge API returned HTTP %d\n", httpCode);
    } else {
        Serial.printf("HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("SecurIoT ESP32-CAM firmware starting");

    if (!initCamera()) {
        Serial.println("FATAL: camera init failed, halting");
        while (true) {
            delay(1000);
        }
    }

    servosInit();
    alertInit();
    wifiConnect();
}

void loop() {
    if (!wifiEnsureConnected()) {
        // Not connected yet, backoff is handled inside wifi_manager, do not
        // block here, just skip this cycle and retry on the next loop().
        delay(200);
        return;
    }

    unsigned long now = millis();
    if (now - g_lastCaptureMs < CAPTURE_INTERVAL_MS) {
        delay(50);
        return;
    }
    g_lastCaptureMs = now;

    captureAndSendFrame();
}
