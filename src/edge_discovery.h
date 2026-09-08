#ifndef EDGE_DISCOVERY_H
#define EDGE_DISCOVERY_H

#include <Arduino.h>

// Resolves the Edge API's base URL ("http://<ip>") for this session.
//
// Tries mDNS first: queries the hostname in EDGE_API_MDNS_HOST (secrets.h,
// e.g. "edge-api" resolves "edge-api.local"). If that is not configured, or
// the query times out (no responder on the LAN, or wrong hostname), falls
// back to the static EDGE_API_URL from secrets.h.
//
// Call once from setup() after wifiConnect(). Call again any time the
// caller sees several consecutive request failures against the
// previously-resolved URL, in case the Edge API's IP changed (DHCP renew,
// host machine restarted).
String resolveEdgeApiBaseUrl();

#endif // EDGE_DISCOVERY_H
