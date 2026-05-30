/**
 * @file OTA.cpp
 * @brief ArduinoOTA handler — WiFi-only firmware updates.
 *
 * The OTA hostname is set to the device unique name so it appears in
 * PlatformIO's upload target list.  No bootloader-over-RS-485 path exists.
 */
#include "OTA.h"
#include <ArduinoOTA.h>
#include "Hardware/Log.h"
#include "Hardware/LED.h"
#include "FileSystem/FSInterface.h"
#include "Functions.h"

static bool _otaActive = false;

/**
 * @brief Configure ArduinoOTA callbacks and start the OTA service.
 *
 * On start: sets @c _otaActive and switches the LED to solid amber.
 * On progress: logs percentage at DEBUG level.
 * On error: logs the error type and clears @c _otaActive.
 */
void OTAsetup() {
    String hn = GetUniqueName(); hn.replace(":", "");
    ArduinoOTA.setHostname(hn.c_str());

    ArduinoOTA.onStart([]() {
        _otaActive = true;
        Log(NOTIFY, "OTA start\n");
        SetLEDStatus(LED_DEFAULT);
        LEDColor(200);
    });

    ArduinoOTA.onEnd([]() {
        _otaActive = false;
        Log(NOTIFY, "OTA complete\n");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Log(DEBUG, "OTA %u%%\n", progress / (total / 100));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        _otaActive = false;
        const char* msg = "Unknown";
        if      (error == OTA_AUTH_ERROR)    msg = "Auth failed";
        else if (error == OTA_BEGIN_ERROR)   msg = "Begin failed";
        else if (error == OTA_CONNECT_ERROR) msg = "Connect failed";
        else if (error == OTA_RECEIVE_ERROR) msg = "Receive failed";
        else if (error == OTA_END_ERROR)     msg = "End failed";
        Log(ERROR, "OTA error: %s\n", msg);
    });

    ArduinoOTA.begin();
    Log(NOTIFY, "OTA ready on host: %s\n", hn.c_str());
}

/** @brief Service pending OTA events — call from the main loop. */
void OTAloop() {
    ArduinoOTA.handle();
}

/** @return @c true while a firmware upload is in progress. */
bool OTAactive() { return _otaActive; }
