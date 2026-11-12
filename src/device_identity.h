#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

// Pure, Arduino-free identity helpers. Kept header-only and dependency-free
// (standard C headers only) so the same code is compiled into the firmware
// and into the host-side Unity tests (`pio test -e native`).
//
// The Device id (Cloud Device UUID) and Zone id are typed by the operator in
// the captive portal and later interpolated into a multipart/form-data body.
// The allowlist below guarantees a stored value can never contain CR/LF,
// quotes or any other byte that could break out of a multipart field.

#include <stddef.h>

#define IDENTITY_TOKEN_MAX_LEN 64

// True only for a non-null string of 1..IDENTITY_TOKEN_MAX_LEN characters made
// exclusively of ASCII letters, digits, '.', '_' and '-'. Accepts Cloud UUIDs
// and the legacy "esp32cam-01" / "zone-01" placeholders.
static inline bool isValidIdentityToken(const char *value) {
    if (value == NULL) {
        return false;
    }
    size_t length = 0;
    for (const char *p = value; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!allowed) {
            return false;
        }
        if (++length > IDENTITY_TOKEN_MAX_LEN) {
            return false;
        }
    }
    return length > 0;
}

// Returns `stored` when it is a valid token, otherwise `fallback`. No
// allocation: the returned pointer is one of the two arguments.
static inline const char *resolveIdentity(const char *stored, const char *fallback) {
    return isValidIdentityToken(stored) ? stored : fallback;
}

#endif // DEVICE_IDENTITY_H
