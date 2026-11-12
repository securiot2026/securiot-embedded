#include "ota_update.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "local_alert.h"
#include "ota_policy.h"
#include "provisioning.h"
#include "reset_button.h"
#include "secrets.h"

// An old secrets.h has no OTA_PASSWORD. The empty default keeps OTA disabled.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

#include "log_mirror.h"

// The error mapping in ota_policy.h depends on these values.
static_assert(OTA_AUTH_ERROR == 0 && OTA_BEGIN_ERROR == 1 && OTA_CONNECT_ERROR == 2 &&
                  OTA_RECEIVE_ERROR == 3 && OTA_END_ERROR == 4,
              "ota_error_t values changed, update otaErrorName in ota_policy.h");

namespace {

bool g_running = false;
bool g_disabledLogged = false;
bool g_callbacksSet = false;
OtaProgress g_progress;

// The update runs synchronously inside ArduinoOTA.handle(), so loop(), the servo
// tick and the capture cycle do not run during the transfer: the servos simply
// hold their last position. The LED goes solid orange.
void onOtaStart() {
    otaProgressReset(g_progress);
    indicateOtaActive();
    Serial.println(OTA_LOG_STARTED);
}

// Because the transfer blocks loop(), this callback keeps the reset button and
// the remote log alive. A long press mid-update erases the config and restarts,
// which only aborts an unfinished image because the running slot is untouched.
void onOtaProgress(unsigned int done, unsigned int total) {
    int step = otaProgressStep(g_progress, done, total);
    if (step >= 0) {
        char line[32];
        otaFormatProgressLog(line, sizeof(line), step);
        Serial.println(line);
    }
    resetButtonPoll();
    logMirrorPoll();
}

void onOtaEnd() {
    Serial.println(OTA_LOG_FINISHED);
    // Lets the line reach the client before the automatic restart.
    logMirrorPoll();
}

void onOtaError(ota_error_t error) {
    char line[40];
    otaFormatErrorLog(line, sizeof(line), (int)error);
    Serial.println(line);
    logMirrorPoll();
    // Leaves the orange state.
    indicateWifiConnected();
}

} // namespace

// OTA is deliberately off while the setup portal is open: the access point is
// open and the update port would be reachable from it. After a runtime WiFi
// loss opens the portal, OTA stays off until the board restarts or is reset
// with the button.
void otaPoll() {
    const bool usable = otaPasswordUsable(OTA_PASSWORD);
    if (!g_disabledLogged && !usable) {
        g_disabledLogged = true;
        Serial.println(OTA_LOG_DISABLED);
    }
    if (!usable) {
        return;
    }

    const OtaAction action =
        otaNextAction(usable, g_running, WiFi.status() == WL_CONNECTED, provisioningStarted());

    if (action == OTA_ACTION_START) {
        if (!g_callbacksSet) {
            ArduinoOTA.onStart(onOtaStart);
            ArduinoOTA.onProgress(onOtaProgress);
            ArduinoOTA.onEnd(onOtaEnd);
            ArduinoOTA.onError(onOtaError);
            g_callbacksSet = true;
        }
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.begin();
        g_running = true;
        IPAddress ip = WiFi.localIP();
        char line[80];
        otaFormatReadyLog(line, sizeof(line), ip[0], ip[1], ip[2], ip[3]);
        Serial.println(line);
    } else if (action == OTA_ACTION_STOP) {
        ArduinoOTA.end();
        g_running = false;
        Serial.println(OTA_LOG_STOPPED);
    }

    if (g_running) {
        ArduinoOTA.handle();
    }
}

void remoteServicesPoll() {
    otaPoll();
    logMirrorPoll();
}
