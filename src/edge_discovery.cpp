#include "edge_discovery.h"

#include <ESPmDNS.h>

#include "secrets.h"

namespace {

const unsigned long kMdnsQueryTimeoutMs = 3000;
bool g_mdnsResponderStarted = false;

} // namespace

String resolveEdgeApiBaseUrl() {
    if (!g_mdnsResponderStarted) {
        // This is the ESP32's own advertised name on the LAN, unrelated to
        // the Edge API's hostname queried below. Any name unique on the
        // network works.
        g_mdnsResponderStarted = MDNS.begin("securiot-esp32cam");
        if (!g_mdnsResponderStarted) {
            Serial.println("mDNS: responder failed to start, will rely on static EDGE_API_URL only");
        }
    }

#ifdef EDGE_API_MDNS_HOST
    if (g_mdnsResponderStarted) {
        Serial.printf("mDNS: querying %s.local ...\n", EDGE_API_MDNS_HOST);
        IPAddress ip = MDNS.queryHost(EDGE_API_MDNS_HOST, kMdnsQueryTimeoutMs);
        if (ip != IPAddress(0, 0, 0, 0)) {
#ifdef EDGE_API_PORT
            String url = "http://" + ip.toString() + ":" + String(EDGE_API_PORT);
#else
            String url = "http://" + ip.toString();
#endif
            Serial.print("mDNS: resolved Edge API at ");
            Serial.println(url);
            return url;
        }
        Serial.println("mDNS: query timed out or no responder, falling back to static EDGE_API_URL");
    }
#endif

#ifdef EDGE_API_URL
    Serial.print("Edge API: using static EDGE_API_URL=");
    Serial.println(EDGE_API_URL);
    return String(EDGE_API_URL);
#else
    Serial.println("FATAL: EDGE_API_MDNS_HOST did not resolve and no static EDGE_API_URL is set in secrets.h");
    return "";
#endif
}
