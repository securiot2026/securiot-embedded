#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

// Runs the ArduinoOTA service (hostname securiot-cam) behind the pure policy in
// ota_policy.h. OTA stays off without a usable OTA_PASSWORD, while WiFi is down
// and while the setup portal is open.
//
// Call otaPoll() only through remoteServicesPoll(), unless you have a reason.
void otaPoll();

// Single entry point for the remote services: runs otaPoll() and then the log
// mirror pump. Call it once per loop() iteration and once per 10 ms slice of
// every blocking wait (the boot-time waits in wifi_manager.cpp), so both
// services also work while the portal blocks setup(). It must never be called
// from inside an ArduinoOTA callback, because handle() is not re-entrant.
void remoteServicesPoll();

#endif // OTA_UPDATE_H
