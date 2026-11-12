#ifndef LOG_MIRROR_H
#define LOG_MIRROR_H

// Read-only mirror of the firmware log on TCP port 23 (log_mirror.cpp).
//
// How it taps the log without rewriting call sites:
//   1. A Print-derived tee object (LogTee) writes every byte to the real UART
//      and also pushes it into a small ring buffer. This header maps the
//      identifier Serial to that object with one macro.
//   2. An esp_log vprintf hook, installed by LogTee::begin, copies every IDF and
//      Arduino library error line into the same ring and then calls the previous
//      handler, so the UART output does not change.
//   The TCP side only reads the ring, so no network I/O ever happens inside a
//   logging call.
//
// Include rule: this header must be the LAST include of every file that uses it,
// because it maps the identifier Serial to the tee. Files that print viewer or
// Edge URLs (camera_preview.cpp, edge_discovery.cpp) deliberately do not include
// it, so those lines stay on the real UART only and never reach the network.
//
// LOG_OVER_WIFI: 1 (default) mirrors the log. 0 removes the ring buffer, the
// server and the macro, and Serial stays the real UART. The default sits here,
// after secrets.h, so an old secrets.h without the macro still builds the same
// way in every translation unit.

#include <Arduino.h>

#include "secrets.h"

#ifndef LOG_OVER_WIFI
#define LOG_OVER_WIFI 1
#endif

#if LOG_OVER_WIFI

class LogTee : public Print {
public:
    // Starts the real UART, then (once) the ring buffer and the esp_log hook.
    void begin(unsigned long baud);
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    void flush() override;
};

extern LogTee gLogTee;

// Starts the server lazily and pumps pending bytes to the one client. Call from
// the Arduino loop task only. Never sleeps, never waits and does bounded work.
void logMirrorPoll();

#ifndef LOG_MIRROR_KEEP_SERIAL
// The core may already map Serial to a hardware port name, so drop that first.
#undef Serial
#define Serial gLogTee
#endif

#else

static inline void logMirrorPoll() {}

#endif // LOG_OVER_WIFI

#endif // LOG_MIRROR_H
