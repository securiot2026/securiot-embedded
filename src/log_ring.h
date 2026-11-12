#ifndef LOG_RING_H
#define LOG_RING_H

// Pure, Arduino-free ring buffer and line formatters for the WiFi log mirror.
// Header-only, standard headers only, so the same code compiles into the
// firmware (log_mirror.cpp) and into the host-side Unity tests
// (`pio test -e native`).
//
// The ring is the only meeting point between the producers (the Serial tee and
// the esp_log hook, which run in any task) and the TCP side (which only reads).
// The formatters only accept numbers, never a string, so no line built here can
// carry a URL, a key, a network name or a password.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_MIRROR_PORT 23
#define LOG_MIRROR_RING_BYTES 2048
#define LOG_MIRROR_CHUNK_BYTES 128
#define LOG_MIRROR_MAX_CHUNKS_PER_POLL 4

#define LOG_MIRROR_BANNER "SecurIoT log mirror (read-only), recent history follows\r\n"
#define LOG_MIRROR_LOG_CONNECTED "Log mirror: client connected"
#define LOG_MIRROR_LOG_DISCONNECTED "Log mirror: client disconnected"

static_assert((LOG_MIRROR_RING_BYTES & (LOG_MIRROR_RING_BYTES - 1)) == 0,
              "the ring size must be a power of two");
static_assert(LOG_MIRROR_CHUNK_BYTES <= LOG_MIRROR_RING_BYTES,
              "a chunk must fit in the ring");

// head counts every byte ever pushed and wraps at 2^32, which is harmless
// because the capacity is a power of two. stored is min(total pushed, cap). The
// byte at position p lives at buf[p & (cap - 1)]. A reader keeps its own cursor
// (a position) and the ring never tracks readers.
struct LogRing {
    uint8_t *buf = NULL;
    uint32_t cap = 0;
    uint32_t head = 0;
    uint32_t stored = 0;
};

// Accepts a non-NULL storage and a capacity that is a power of two of at least
// 2. Otherwise returns false and leaves cap at 0, which turns push into a no-op
// and copy into a function that returns 0.
static inline bool logRingInit(LogRing &r, uint8_t *storage, uint32_t cap) {
    r.buf = NULL;
    r.cap = 0;
    r.head = 0;
    r.stored = 0;
    if (storage == NULL || cap < 2 || (cap & (cap - 1)) != 0) {
        return false;
    }
    r.buf = storage;
    r.cap = cap;
    return true;
}

// Appends len bytes. A push longer than the capacity keeps only the newest cap
// bytes but still advances head by the full length, so a reader sees exactly how
// many bytes were lost.
static inline void logRingPush(LogRing &r, const uint8_t *data, size_t len) {
    if (r.cap == 0 || data == NULL || len == 0) {
        return;
    }
    size_t keep = len > r.cap ? r.cap : len;
    const uint8_t *src = data + (len - keep);
    uint32_t newHead = r.head + (uint32_t)len;
    uint32_t pos = newHead - (uint32_t)keep;
    uint32_t mask = r.cap - 1;
    for (size_t i = 0; i < keep; i++) {
        r.buf[(pos + (uint32_t)i) & mask] = src[i];
    }
    r.head = newHead;
    uint64_t total = (uint64_t)r.stored + (uint64_t)len;
    r.stored = total > r.cap ? r.cap : (uint32_t)total;
}

// Position of the oldest byte still held.
static inline uint32_t logRingOldest(const LogRing &r) {
    return r.head - r.stored;
}

// Moves a cursor that fell behind the retained window up to the oldest byte.
// dropped (optional) receives how many bytes the reader missed, 0 when the
// cursor is still inside the window.
static inline uint32_t logRingClamp(const LogRing &r, uint32_t cursor, uint32_t *dropped) {
    uint32_t behind = r.head - cursor;
    if (behind > r.stored) {
        if (dropped != NULL) {
            *dropped = behind - r.stored;
        }
        return logRingOldest(r);
    }
    if (dropped != NULL) {
        *dropped = 0;
    }
    return cursor;
}

// Copies min(available, maxLen) bytes starting at cursor and returns the count.
// Returns 0 for a cursor that is not inside the window (clamp it first) and for
// an uninitialized ring. Does not move the cursor, the caller adds the count.
static inline size_t logRingCopy(const LogRing &r, uint32_t cursor, uint8_t *out, size_t maxLen) {
    if (r.cap == 0 || out == NULL || maxLen == 0) {
        return 0;
    }
    uint32_t avail = r.head - cursor;
    if (avail > r.stored) {
        return 0;
    }
    size_t n = avail < maxLen ? avail : maxLen;
    uint32_t mask = r.cap - 1;
    for (size_t i = 0; i < n; i++) {
        out[i] = r.buf[(cursor + (uint32_t)i) & mask];
    }
    return n;
}

// Where a new client starts reading. Without any loss it is the oldest byte.
// After the ring wrapped, the oldest byte is probably in the middle of a line,
// so it is the position just after the first newline in the window (or the
// oldest byte when the window holds no newline). This may skip one complete
// line, because the byte before the window is gone and nothing says whether the
// window starts on a line boundary.
static inline uint32_t logRingHistoryStart(const LogRing &r) {
    uint32_t oldest = logRingOldest(r);
    if (r.cap == 0 || r.stored < r.cap || r.head == r.stored) {
        return oldest;
    }
    uint32_t mask = r.cap - 1;
    for (uint32_t i = 0; i < r.stored; i++) {
        if (r.buf[(oldest + i) & mask] == '\n') {
            return oldest + i + 1;
        }
    }
    return oldest;
}

// Writes CR LF, `[log mirror: skipped <n> bytes]`, CR LF. Always NUL-terminates
// when outSize is at least 1, writes nothing for a NULL out or outSize 0.
// Returns the snprintf result (0 for the no-op cases).
static inline int logFormatSkippedLine(char *out, size_t outSize, uint32_t skipped) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "\r\n[log mirror: skipped %lu bytes]\r\n", (unsigned long)skipped);
}

// Writes `Log mirror: listening on port <port>`. Same NULL and size contract.
static inline int logFormatListeningLog(char *out, size_t outSize, unsigned port) {
    if (out == NULL || outSize == 0) {
        return 0;
    }
    return snprintf(out, outSize, "Log mirror: listening on port %u", port);
}

#endif // LOG_RING_H
