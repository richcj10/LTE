/**
 * @file Functions.cpp
 * @brief System startup sequencer and main-loop dispatcher.
 *
 * Startup() initialises every subsystem in dependency order.
 * RunLoop() is the non-blocking per-iteration dispatcher called from loop().
 */
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

static String UN = "";

static unsigned long UpdatePreviousMillis     = 0;
static unsigned long MQTTUpdatePreviousMillis = 0;
static unsigned long DebugPreviousMillis      = 0;

static char lastPowerMode  = ACON;
static char WiFiErrorCount = 0;

/**
 * @brief Full system initialisation in dependency order.
 *
 * Sequence: GPIO → NeoPixel → unique name → file system → fuel gauge →
 * RS-485 or serial debug → WiFi → web portal → OTA → MQTT → cellular task.
 *
 * @param WifiEnable 1 = start WiFi/web/OTA/MQTT subsystems.
 * @param LTEEnable  1 = start the cellular FreeRTOS task on Core 0.
 * @return Always @c 1.
 */
char Startup(bool WifiEnable, bool LTEEnable) {
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
            WebStart();
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

/**
 * @brief Build the unique device name from the last 5 MAC nibbles.
 *
 * Format: @c "ESPPLC-LTE-XXXXX" where XXXXX is the tail of the WiFi MAC.
 * Must be called before CellTaskStart() as the cellular task copies this name.
 */
void UniqueName() {
    String Mac = WiFi.macAddress();
    int Len = Mac.length();
    UN  = "ESPPLC-LTE-";
    UN += Mac.charAt(Len - 5);
    UN += Mac.charAt(Len - 4);
    UN += Mac.charAt(Len - 3);
    UN += Mac.charAt(Len - 2);
    UN += Mac.charAt(Len - 1);
    Log(LOG, "Device: %s\n", UN.c_str());
}

/** @return Device unique name string (populated by UniqueName()). */
String GetUniqueName() { return UN; }

/**
 * @brief Non-blocking per-iteration loop dispatcher.
 *
 * Drives OTA, web portal, NeoPixel, WiFi AP, MQTT, and RS-485 every call.
 * Every UPDATE_LOOP ms: toggles debug LED, polls fuel gauge, updates NTP,
 * and detects power-loss/restore transitions to send Pushover alerts.
 * Every MQTT_UPDATE_LOOP ms: checks WiFi health and publishes MQTT state.
 */
void RunLoop() {
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
            if (++WiFiErrorCount > 3) ESP.restart();
        }
        MQTTMessageUpdate();
    }
}

/**
 * @brief Periodic debug print — fires FGDisplay() and PrintTime() every DEBUG_LOOP ms.
 */
void DebugPrint() {
    if (millis() - DebugPreviousMillis >= DEBUG_LOOP) {
        DebugPreviousMillis = millis();
        FGDisplay();
        PrintTime();
    }
}
