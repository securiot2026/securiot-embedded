#define LOG_MIRROR_KEEP_SERIAL

#include "log_mirror.h"

#include <WiFi.h>
#include <errno.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <stdarg.h>
#include <stdio.h>

#include "log_ring.h"

// Read-only TCP mirror of the firmware log, see log_mirror.h for the design.
//
// This file never touches the saved config, the WiFi identity, the device key,
// the Edge URL, the OTA password or the address of the client. It only moves
// bytes that the rest of the firmware already printed. Nothing here sleeps or
// waits: every poll does a bounded amount of non-blocking work, and the client
// socket is only written with a raw non-blocking send (the WiFiClient write
// methods can stall the caller for up to 10 seconds on a slow peer).

#if LOG_OVER_WIFI

namespace {

const unsigned long kServerRetryMs = 5000;
const int kMaxAcceptsPerPoll = 3;
const size_t kDiscardBytesPerPoll = 64;
const size_t kHookBufferBytes = 160;

uint8_t s_ringStorage[LOG_MIRROR_RING_BYTES];
LogRing s_ring;
portMUX_TYPE s_ringMux = portMUX_INITIALIZER_UNLOCKED;
bool s_hookInstalled = false;
vprintf_like_t s_prevVprintf = NULL;

WiFiServer s_server(LOG_MIRROR_PORT, 2);
WiFiClient s_client;
bool s_haveClient = false;
uint32_t s_cursor = 0;
bool s_serverStarted = false;
bool s_startFailed = false;
unsigned long s_lastStartFailMs = 0;

// The only writer of the ring. The lock covers a bounded copy and nothing else:
// no logging, no network call, no allocation.
void ringPush(const uint8_t *data, size_t len) {
    portENTER_CRITICAL(&s_ringMux);
    logRingPush(s_ring, data, len);
    portEXIT_CRITICAL(&s_ringMux);
}

// vprintf hook for IDF and Arduino library lines. Formats into a small stack
// buffer, copies the text into the ring, then calls the previous handler with
// the original arguments so the UART output is unchanged. No heap, no logging.
int mirrorVprintf(const char *fmt, va_list args) {
    char buf[kHookBufferBytes];
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
        ringPush((const uint8_t *)buf, len);
    }
    if (s_prevVprintf != NULL) {
        return s_prevVprintf(fmt, args);
    }
    return vprintf(fmt, args);
}

// Raw non-blocking send. Returns the bytes accepted by the TCP stack (0 when it
// would block) or -1 on any other error.
int rawSend(int fd, const uint8_t *buf, size_t len) {
    int r = send(fd, buf, len, MSG_DONTWAIT);
    if (r >= 0) {
        return r;
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return 0;
    }
    return -1;
}

void dropClient() {
    s_client.stop();
    s_haveClient = false;
    gLogTee.println(LOG_MIRROR_LOG_DISCONNECTED);
}

void startServerIfNeeded() {
    if (s_serverStarted) {
        return;
    }
    // Lazy start on the radio mode, not on an IP address, so the mirror also
    // works while the setup portal is the only network that is up.
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        return;
    }
    unsigned long now = millis();
    if (s_startFailed && (unsigned long)(now - s_lastStartFailMs) < kServerRetryMs) {
        return;
    }
    s_server.begin();
    s_server.setNoDelay(true);
    if (s_server) {
        s_serverStarted = true;
        s_startFailed = false;
        char line[48];
        logFormatListeningLog(line, sizeof(line), LOG_MIRROR_PORT);
        gLogTee.println(line);
    } else {
        // A failed begin can leave a half-open socket, close it before the retry.
        s_server.end();
        s_startFailed = true;
        s_lastStartFailMs = now;
    }
}

void acceptNewClient() {
    bool accepted = false;
    for (int i = 0; i < kMaxAcceptsPerPoll && s_server.hasClient(); i++) {
        WiFiClient fresh = s_server.accept();
        if (fresh.fd() < 0) {
            continue;
        }
        // The newest connection wins, so a stale session after a laptop sleep
        // never locks the log out.
        if (s_haveClient) {
            s_client.stop();
        }
        s_client = fresh;
        s_haveClient = true;
        accepted = true;
    }
    if (!accepted) {
        return;
    }
    portENTER_CRITICAL(&s_ringMux);
    s_cursor = logRingHistoryStart(s_ring);
    portEXIT_CRITICAL(&s_ringMux);
    const char banner[] = LOG_MIRROR_BANNER;
    if (rawSend(s_client.fd(), (const uint8_t *)banner, sizeof(banner) - 1) < 0) {
        dropClient();
        return;
    }
    gLogTee.println(LOG_MIRROR_LOG_CONNECTED);
}

// The log is read-only: pending input is read and thrown away. The same call
// tells a closed or broken peer apart (connected() can stay true after a FIN).
// Returns false when the client is gone.
bool clientStillThere() {
    uint8_t sink[kDiscardBytesPerPoll];
    int r = recv(s_client.fd(), sink, sizeof(sink), MSG_DONTWAIT);
    if (r == 0) {
        return false;
    }
    if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
        return false;
    }
    return true;
}

void pumpToClient() {
    for (int round = 0; round < LOG_MIRROR_MAX_CHUNKS_PER_POLL; round++) {
        uint8_t chunk[LOG_MIRROR_CHUNK_BYTES];
        uint32_t dropped = 0;
        size_t n = 0;
        portENTER_CRITICAL(&s_ringMux);
        s_cursor = logRingClamp(s_ring, s_cursor, &dropped);
        n = logRingCopy(s_ring, s_cursor, chunk, sizeof(chunk));
        portEXIT_CRITICAL(&s_ringMux);

        if (dropped > 0) {
            char marker[48];
            int mlen = logFormatSkippedLine(marker, sizeof(marker), dropped);
            if (mlen > 0 && rawSend(s_client.fd(), (const uint8_t *)marker, (size_t)mlen) < 0) {
                dropClient();
                return;
            }
        }
        if (n == 0) {
            return;
        }
        int sent = rawSend(s_client.fd(), chunk, n);
        if (sent < 0) {
            dropClient();
            return;
        }
        s_cursor += (uint32_t)sent;
        if ((size_t)sent < n) {
            return;
        }
    }
}

} // namespace

LogTee gLogTee;

void LogTee::begin(unsigned long baud) {
    ::Serial.begin(baud);
    if (!s_hookInstalled) {
        portENTER_CRITICAL(&s_ringMux);
        logRingInit(s_ring, s_ringStorage, sizeof(s_ringStorage));
        portEXIT_CRITICAL(&s_ringMux);
        s_prevVprintf = esp_log_set_vprintf(mirrorVprintf);
        s_hookInstalled = true;
    }
}

size_t LogTee::write(uint8_t b) {
    size_t n = ::Serial.write(b);
    ringPush(&b, 1);
    return n;
}

size_t LogTee::write(const uint8_t *buffer, size_t size) {
    size_t n = ::Serial.write(buffer, size);
    ringPush(buffer, size);
    return n;
}

void LogTee::flush() {
    ::Serial.flush();
}

void logMirrorPoll() {
    startServerIfNeeded();
    if (!s_serverStarted) {
        return;
    }
    acceptNewClient();
    if (!s_haveClient) {
        return;
    }
    if (!clientStillThere()) {
        dropClient();
        return;
    }
    pumpToClient();
}

#endif // LOG_OVER_WIFI
