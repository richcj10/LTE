#include "Functions.h"
#include <WiFi.h>
#include "Hardware/LED.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"
#include "Hardware/RS485.h"
#include "Define.h"
#include "Hardware/Log.h"
#include "Hardware/IO.h"
#include "Comunication/Webportal.h"
#include "Comunication/Wifi.h"
#include "Comunication/OTA.h"
#include "FileSystem/FSInterface.h"
#include "Comunication/MQTT.h"

String UN = "";

unsigned long UpdatePreviousMillis     = 0;
unsigned long MQTTUpdatePreviousMillis = 0;
unsigned long DebugPreviousMillis      = 0;
unsigned long WiFipreviousMillis       = 0;
unsigned long RS485PreviousMillis      = 0;

char WiFiErrorCount = 0;
char lastPowerMode  = ACON;

char Startup(bool WifiEnable, bool LTEEnable){
    ConfigIO();
    LEDsetup();
    LEDColor(40);
    UniqueName();
    FileStstemStart();
    FGsetup(0);
    if (GetRS485Mode()) {
        RS485setup();
    } else {
        Serial.begin(115200);
        SetSerialLog(true);
    }
    if (WifiEnable) {
        char wfr = WiFiBootSequence();
        if (wfr == 1 || wfr == 2) {
            WebStart();   /* main web UI serves in both STA and AP modes */
            OTAsetup();
        }
        if (wfr == 1) {
            MQTTStart();
        }
    }
    if (LTEEnable) {
        CellTaskStart();
    }
    return 1;
}


void UniqueName(){
    String Mac = WiFi.macAddress();
    int Len = Mac.length();
    UN = "ESPPLC-LTE-";
    UN += Mac.charAt(Len - 5);
    UN += Mac.charAt(Len - 4);
    UN += Mac.charAt(Len - 3);
    UN += Mac.charAt(Len - 2);
    UN += Mac.charAt(Len - 1);
    Log(LOG, "Device: %s\n", UN.c_str());
}

String GetUniqueName(){
    return UN;
}

void RunLoop(){
    OTAloop();
    WebHandel();
    LEDUpdate();
    WiFiAPProcess();
    MqttLoop();

    if (GetRS485Mode()) RS485loop();

    if (millis() - UpdatePreviousMillis >= UPDATE_LOOP) {
        UpdatePreviousMillis = millis();
        DebugLEDToggle();
        FGloop();
        UpdateTime();
        if ((GetPowerMode() == BUBON) && (lastPowerMode == ACON)) {
            lastPowerMode = BUBON;
            Pushover("LTE Modem", "Lost Power!");
        }
        if ((GetPowerMode() == ACON) && (lastPowerMode == BUBON)) {
            lastPowerMode = ACON;
            Pushover("LTE Modem", "Power Restored!");
        }
    }

    if (millis() - MQTTUpdatePreviousMillis >= MQTT_UPDATE_LOOP) {
        MQTTUpdatePreviousMillis = millis();
        if (!WiFiIsAPMode() && WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect();
            WiFi.reconnect();
            WiFiErrorCount++;
            if (WiFiErrorCount > 3) ESP.restart();
        }
        MQTTMessageUpdate();
    }
}

void DebugPrint(){
    if (millis() - DebugPreviousMillis >= DEBUG_LOOP) {
        DebugPreviousMillis = millis();
        FGDisplay();
        // CellularDisplay();
        PrintTime();
    }
}
