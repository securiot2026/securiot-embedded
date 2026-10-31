#ifndef PORTAL_PREFILL_H
#define PORTAL_PREFILL_H

// Pure, Arduino-free helpers for the "Setup QR" prefill of the provisioning
// portal page /provision-extra. Header-only, standard headers only, so the same
// code compiles into the firmware and into the host-side Unity tests
// (`pio test -e native`).
//
// The four values arrive as query args of a link anyone on the open setup
// access point can craft, and are reflected into an HTML page. Two layers:
//   1. validation per field (allowlists, length caps), invalid values are
//      dropped and the field stays empty;
//   2. escaping for a double-quoted attribute at output time. Escaping is the
//      primary defence, validation is the second layer.
// Values are only ever placed inside double-quoted value attributes.

#include <stddef.h>

#include <string>

#include "device_identity.h"

#define PORTAL_PREFILL_URL_MAX_LEN 200
#define PORTAL_PREFILL_KEY_MAX_LEN 128

// Escapes & < > " ' for a double-quoted HTML attribute, drops bytes below 0x20
// and 0x7F (CR, LF, TAB and other controls), lets bytes >= 0x80 pass through.
// NULL is treated as an empty string.
static inline std::string htmlEscapeAttribute(const char *raw) {
    std::string out;
    if (raw == NULL) {
        return out;
    }
    for (const char *p = raw; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    break; // dropped
                }
                out += (char)c;
                break;
        }
    }
    return out;
}

// Case-insensitive prefix check against a lower-case ASCII prefix.
static inline bool prefillStartsWithIgnoreCase(const char *value, const char *lowerPrefix) {
    while (*lowerPrefix != '\0') {
        if (*value == '\0') {
            return false;
        }
        char c = *value;
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != *lowerPrefix) {
            return false;
        }
        ++value;
        ++lowerPrefix;
    }
    return true;
}

// True for http:// or https:// (any case) followed by at least one character,
// at most PORTAL_PREFILL_URL_MAX_LEN characters in total, every byte printable
// ASCII 0x21..0x7E excluding " ' < > \ and backtick.
static inline bool isValidPrefillEdgeUrl(const char *raw) {
    if (raw == NULL) {
        return false;
    }
    size_t schemeLen = 0;
    if (prefillStartsWithIgnoreCase(raw, "https://")) {
        schemeLen = 8;
    } else if (prefillStartsWithIgnoreCase(raw, "http://")) {
        schemeLen = 7;
    } else {
        return false;
    }
    size_t length = 0;
    for (const char *p = raw; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x21 || c > 0x7E) {
            return false;
        }
        if (c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\' || c == '`') {
            return false;
        }
        if (++length > PORTAL_PREFILL_URL_MAX_LEN) {
            return false;
        }
    }
    return length > schemeLen;
}

// True for 1..PORTAL_PREFILL_KEY_MAX_LEN printable ASCII bytes 0x21..0x7E (no
// spaces). Special characters are legal in a key; the escaper neutralises them
// at output time.
static inline bool isValidPrefillDeviceKey(const char *raw) {
    if (raw == NULL) {
        return false;
    }
    size_t length = 0;
    for (const char *p = raw; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x21 || c > 0x7E) {
            return false;
        }
        if (++length > PORTAL_PREFILL_KEY_MAX_LEN) {
            return false;
        }
    }
    return length > 0;
}

struct PortalPrefill {
    std::string edgeApiUrl;
    std::string deviceKey;
    std::string deviceId;
    std::string zoneId;

    int count() const {
        return (edgeApiUrl.empty() ? 0 : 1) + (deviceKey.empty() ? 0 : 1) +
               (deviceId.empty() ? 0 : 1) + (zoneId.empty() ? 0 : 1);
    }
    bool any() const { return count() > 0; }
};

// Fills each field only when its own validator passes, independently per
// field. A NULL argument (missing query arg) leaves the field empty.
static inline PortalPrefill sanitizePortalPrefill(const char *edgeApiUrl, const char *deviceKey,
                                                  const char *deviceId, const char *zoneId) {
    PortalPrefill out;
    if (isValidPrefillEdgeUrl(edgeApiUrl)) {
        out.edgeApiUrl = edgeApiUrl;
    }
    if (isValidPrefillDeviceKey(deviceKey)) {
        out.deviceKey = deviceKey;
    }
    if (isValidIdentityToken(deviceId)) {
        out.deviceId = deviceId;
    }
    if (isValidIdentityToken(zoneId)) {
        out.zoneId = zoneId;
    }
    return out;
}

// Returns an empty string for empty input, otherwise ` value="<escaped>"`
// (leading space, ready to sit right before an input tag closes).
static inline std::string valueAttribute(const std::string &sanitized) {
    if (sanitized.empty()) {
        return std::string();
    }
    return std::string(" value=\"") + htmlEscapeAttribute(sanitized.c_str()) + "\"";
}

#endif // PORTAL_PREFILL_H
