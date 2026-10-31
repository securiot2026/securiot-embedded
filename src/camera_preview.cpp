#include "camera_preview.h"

#include <atomic>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

namespace {

httpd_handle_t previewServer = nullptr;
httpd_handle_t streamServer = nullptr;
std::atomic<uint32_t> streamFrames{0};
std::atomic<uint32_t> streamFps100{0};
std::atomic<bool> streamActive{false};

esp_err_t stream(httpd_req_t *request) {
    esp_err_t result = httpd_resp_set_type(
        request, "multipart/x-mixed-replace;boundary=securiot-frame");
    if (result != ESP_OK) return result;
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    streamActive = true;
    streamFps100 = 0;
    uint32_t windowStart = millis();
    uint32_t windowFrames = 0;

    while (result == ESP_OK) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (!frame) {
            result = ESP_FAIL;
            break;
        }

        char header[96];
        const int length = snprintf(header, sizeof(header),
            "\r\n--securiot-frame\r\nContent-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n", static_cast<unsigned>(frame->len));
        result = httpd_resp_send_chunk(request, header, length);
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(request,
                reinterpret_cast<const char *>(frame->buf), frame->len);
        }
        // Release the buffer even when the viewer disconnects mid-frame.
        esp_camera_fb_return(frame);
        if (result == ESP_OK) {
            ++streamFrames;
            ++windowFrames;
            const uint32_t elapsed = millis() - windowStart;
            if (elapsed >= 1000) {
                streamFps100 = windowFrames * 100000UL / elapsed;
                windowFrames = 0;
                windowStart = millis();
            }
        }
        delay(1);
    }
    streamActive = false;
    streamFps100 = 0;
    return result;
}

const char page[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" href="data:,">
<title>SecurIoT Camera</title>
<style>
body{margin:0;background:#101b25;color:#e5f0f3;font:16px monospace}
main{max-width:960px;margin:40px auto;padding:0 20px}
h1{font-size:24px}p{color:#a7bdc7;line-height:1.6}
img{display:block;width:100%;height:auto;aspect-ratio:4/3;object-fit:contain;background:#060b10}
button,a{display:inline-block;padding:12px;color:#b9eee3;background:#1a343e;border:1px solid #5a827f;font:inherit}
button{cursor:pointer}nav{display:flex;gap:12px;margin:20px 0;flex-wrap:wrap}
</style>
<main>
<h1>SecurIoT / Camera preview</h1>
<p>Continuous MJPEG video · 640 × 480 · No Edge uploads or servo control</p>
<img id="camera" alt="Live view from the ESP32 camera" width="640" height="480">
<nav><button id="toggle" type="button">Pause preview</button><a href="/capture" target="_blank" rel="noopener">Open snapshot</a></nav>
<p id="status" role="status">Connecting to camera…</p>
<p>Trusted network only. Anyone on this network can access this preview.</p>
</main>
<script>
const camera=document.getElementById('camera');
const status=document.getElementById('status');
const toggle=document.getElementById('toggle');
let paused=false,failed=false,timer;
function start(){
  clearTimeout(timer);
  if(paused||document.hidden)return;
  failed=false;
  status.textContent='Connecting to camera…';
  camera.src='http://'+location.hostname+':81/stream?t='+Date.now();
}
function stop(){clearTimeout(timer);camera.removeAttribute('src')}
camera.onerror=()=>{if(paused||document.hidden)return;failed=true;status.textContent='Camera unavailable. Retrying…';clearTimeout(timer);timer=setTimeout(start,2000)};
toggle.onclick=()=>{paused=!paused;toggle.textContent=paused?'Resume preview':'Pause preview';stop();if(paused)status.textContent='Preview paused';else start()};
document.addEventListener('visibilitychange',()=>{stop();start()});
let checkingStatus=false;
setInterval(async()=>{
  if(paused||failed||document.hidden||checkingStatus)return;
  checkingStatus=true;
  try{
    const response=await fetch('/status',{cache:'no-store',signal:AbortSignal.timeout(3000)});
    if(!response.ok)return;
    const data=await response.json();
    if(!paused&&!document.hidden)status.textContent=data.streaming?'MJPEG video · '+data.fps.toFixed(1)+' FPS':'Connecting to camera…';
  }catch{}
  finally{checkingStatus=false}
},1000);
start();
</script>
</html>)HTML";

esp_err_t capture(httpd_req_t *request) {
    camera_fb_t *frame = esp_camera_fb_get();
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    if (!frame) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_set_type(request, "text/plain");
        return httpd_resp_send(request, "Camera capture failed", HTTPD_RESP_USE_STRLEN);
    }
    // The synchronous send finishes before the camera buffer is released.
    httpd_resp_set_type(request, "image/jpeg");
    esp_err_t result = httpd_resp_send(request,
        reinterpret_cast<const char *>(frame->buf), frame->len);
    esp_camera_fb_return(frame);
    return result;
}

} // namespace

void cameraPreviewBegin() {
    // Keep the long-lived stream on a separate HTTP task so the viewer
    // page and snapshot endpoint remain responsive on port 80.
    WiFi.setSleep(false);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.send_wait_timeout = 3;
    esp_err_t result = httpd_start(&streamServer, &config);
    if (result == ESP_OK) {
        httpd_uri_t endpoint = {};
        endpoint.uri = "/stream";
        endpoint.method = HTTP_GET;
        endpoint.handler = stream;
        result = httpd_register_uri_handler(streamServer, &endpoint);
        if (result != ESP_OK) {
            httpd_stop(streamServer);
            streamServer = nullptr;
        }
    }
    if (result != ESP_OK) {
        Serial.printf("Camera stream start failed: 0x%x\n", result);
    }
    httpd_config_t previewConfig = HTTPD_DEFAULT_CONFIG();
    previewConfig.ctrl_port += 1;
    previewConfig.max_open_sockets = 2;
    previewConfig.lru_purge_enable = true;
    result = httpd_start(&previewServer, &previewConfig);
    if (result != ESP_OK) {
        Serial.printf("Camera viewer start failed: 0x%x\n", result);
        return;
    }
    httpd_uri_t index = {};
    index.uri = "/";
    index.method = HTTP_GET;
    index.handler = [](httpd_req_t *request) -> esp_err_t {
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        return httpd_resp_send(request, page, sizeof(page) - 1);
    };
    httpd_uri_t snapshot = {};
    snapshot.uri = "/capture";
    snapshot.method = HTTP_GET;
    snapshot.handler = capture;
    httpd_uri_t status = {};
    status.uri = "/status";
    status.method = HTTP_GET;
    status.handler = [](httpd_req_t *request) -> esp_err_t {
        char body[128];
        snprintf(body, sizeof(body), "{\"streaming\":%s,\"frames\":%lu,\"fps\":%.2f}",
                 streamActive.load() ? "true" : "false",
                 static_cast<unsigned long>(streamFrames.load()),
                 streamFps100.load() / 100.0);
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    };
    if (httpd_register_uri_handler(previewServer, &index) != ESP_OK ||
        httpd_register_uri_handler(previewServer, &snapshot) != ESP_OK ||
        httpd_register_uri_handler(previewServer, &status) != ESP_OK) {
        Serial.println("Camera viewer route registration failed");
        httpd_stop(previewServer);
        previewServer = nullptr;
        return;
    }
    Serial.printf("Camera preview: http://%s/\n", WiFi.localIP().toString().c_str());
    if (streamServer) {
        Serial.printf("Camera MJPEG stream: http://%s:81/stream\n",
                      WiFi.localIP().toString().c_str());
    }
}
