#include "OTA.h"
#include <ArduinoOTA.h>
#include "Hardware/Log.h"
#include "Hardware/LED.h"
#include "FileSystem/FSInterface.h"

static bool _otaActive = false;

void OTAsetup() {
    ArduinoOTA.setHostname(GetHostName().c_str());

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
    Log(NOTIFY, "OTA ready on host: %s\n", GetHostName().c_str());
}

void OTAloop() {
    ArduinoOTA.handle();
}

bool OTAactive() { return _otaActive; }
