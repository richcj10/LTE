#include "MQTT.h"
#include "Functions.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Wifi.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"
#include "Hardware/Log.h"
#include "FileSystem/FSInterface.h"

#define MQTTid      "CellDevice"
#define MQTTpubQos  1
#define MQTTsubQos  1
#define HA_PREFIX   "homeassistant"

char MQTTActive  = 0;
char MQTTLockout = 0;
char MsgType     = 0;

static char mqttBrokerIP[16];
static char _haDevId[32]    = "";   /* "espplc_lte_a1b2"   */
static char _haDevJson[180] = "";   /* pre-built device block */

WiFiClient    wclient;
PubSubClient  client(wclient);
StaticJsonDocument<200> jsonmqttRx;

// ── HA discovery helpers ──────────────────────────────────────

/* Publish a retained sensor discovery config */
static void haPublishSensor(const char* objId, const char* name,
                             const char* stateTopic, const char* unit,
                             const char* devClass) {
    char topic[128];
    char payload[512];
    snprintf(topic, sizeof(topic),
             HA_PREFIX "/sensor/%s/%s/config", _haDevId, objId);
    if (devClass && devClass[0]) {
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"state_topic\":\"%s\","
            "\"unit_of_measurement\":\"%s\",\"device_class\":\"%s\","
            "\"unique_id\":\"%s_%s\",\"device\":%s}",
            name, stateTopic, unit, devClass, _haDevId, objId, _haDevJson);
    } else {
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"state_topic\":\"%s\","
            "\"unit_of_measurement\":\"%s\","
            "\"unique_id\":\"%s_%s\",\"device\":%s}",
            name, stateTopic, unit, _haDevId, objId, _haDevJson);
    }
    client.publish(topic, payload, true);
}

/* Publish a retained binary_sensor discovery config */
static void haPublishBinary(const char* objId, const char* name,
                             const char* stateTopic, const char* devClass) {
    char topic[128];
    char payload[512];
    snprintf(topic, sizeof(topic),
             HA_PREFIX "/binary_sensor/%s/%s/config", _haDevId, objId);
    if (devClass && devClass[0]) {
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"state_topic\":\"%s\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"device_class\":\"%s\","
            "\"unique_id\":\"%s_%s\",\"device\":%s}",
            name, stateTopic, devClass, _haDevId, objId, _haDevJson);
    } else {
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"state_topic\":\"%s\","
            "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
            "\"unique_id\":\"%s_%s\",\"device\":%s}",
            name, stateTopic, _haDevId, objId, _haDevJson);
    }
    client.publish(topic, payload, true);
}

/* Publish discovery for a text sensor (no unit) */
static void haPublishText(const char* objId, const char* name,
                           const char* stateTopic) {
    char topic[128];
    char payload[512];
    snprintf(topic, sizeof(topic),
             HA_PREFIX "/sensor/%s/%s/config", _haDevId, objId);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"state_topic\":\"%s\","
        "\"unique_id\":\"%s_%s\",\"device\":%s}",
        name, stateTopic, _haDevId, objId, _haDevJson);
    client.publish(topic, payload, true);
}

static void MQTTPublishDiscovery() {
    if (_haDevId[0] == '\0') return;
    Log(NOTIFY, "MQTT: publishing HA discovery\n");

    /* ── Battery ──────────────────────────────────────────────── */
    haPublishSensor("batt_v",    "Battery Voltage",     "Cellular/CellVoltage", "V",   "voltage");
    haPublishSensor("batt_soc",  "Battery SoC",         "Cellular/CellPWR",     "%",   "battery");
    haPublishSensor("batt_rate", "Battery Charge Rate", "Cellular/CellRate",    "%/h", "");
    haPublishText  ("pwr_src",   "Power Source",        "Cellular/PowerMode");
    haPublishBinary("batt_alert","Battery Alert",       "Cellular/CellAlert",   "battery");

    /* ── Cellular ─────────────────────────────────────────────── */
    haPublishSensor("cell_rssi", "Cell RSSI",    "Cellular/CellSignal",    "dBm", "signal_strength");
    haPublishText  ("cell_stat", "Cell Status",  "Cellular/CellStatus");
    haPublishBinary("cell_conn", "Cell Connected","Cellular/CellConnected", "connectivity");

    /* ── GPS ──────────────────────────────────────────────────── */
    haPublishBinary("gps_fix",  "GPS Fix",       "Cellular/GPSFix",     "");
    haPublishSensor("gps_lat",  "GPS Latitude",  "Cellular/GPSLat",     "°",    "");
    haPublishSensor("gps_lon",  "GPS Longitude", "Cellular/GPSLon",     "°",    "");
    haPublishSensor("gps_spd",  "GPS Speed",     "Cellular/GPSSpeed",   "km/h", "speed");
    haPublishSensor("gps_alt",  "GPS Altitude",  "Cellular/GPSAlt",     "m",    "distance");
    haPublishSensor("gps_hdg",  "GPS Heading",   "Cellular/GPSHeading", "°",    "");

    Log(NOTIFY, "MQTT: HA discovery complete (14 entities)\n");
}

// ── Callback ──────────────────────────────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, "Cellular/CellMsg") == 0) {
        deserializeJson(jsonmqttRx, payload);
        const char* MsgTitle = jsonmqttRx["Title"];
        const char* MsgTxLoc = jsonmqttRx["Loc"];
        const char* Msg      = jsonmqttRx["Msg"];
        Log(LOG, "MQTT msg: %s\n", MsgTitle);
        if (strcmp(MsgTxLoc, "PO") == 0) {
            client.publish("Cellular/Status", "Sending PO...");
            bool result = Pushover(MsgTitle, Msg);
            client.publish("Cellular/Status", result ? "Msg OK" : "Msg Failed");
        }
    }
}

// ── Loop / reconnect ──────────────────────────────────────────
void MqttLoop(void) {
    if (MQTTLockout) {
        static unsigned long lockoutStart = 0;
        if (lockoutStart == 0) lockoutStart = millis();
        if (millis() - lockoutStart > 120000UL) {
            MQTTLockout = 0;
            lockoutStart = 0;
        }
        return;
    }
    if (client.connected()) {
        MQTTActive = 1;
        client.loop();
    } else {
        MQTTActive = 0;
        MQTTreconnect();
    }
}

void MQTTStart() {
    /* Build device ID: lower-cased unique name, hyphens→underscores */
    String un = GetUniqueName();
    String id = un;
    id.toLowerCase();
    id.replace("-", "_");
    strlcpy(_haDevId, id.c_str(), sizeof(_haDevId));
    snprintf(_haDevJson, sizeof(_haDevJson),
        "{\"identifiers\":[\"%s\"],\"name\":\"%s\","
        "\"manufacturer\":\"Custom\",\"model\":\"ESP32-LTE-SIM7000\"}",
        _haDevId, un.c_str());

    strlcpy(mqttBrokerIP, GetMQTTIP().c_str(), sizeof(mqttBrokerIP));
    client.setBufferSize(512);          /* need room for discovery payloads */
    client.setServer(mqttBrokerIP, GetMQTTPort());
    client.setKeepAlive(60);
    client.setCallback(callback);
}

void MQTTreconnect(void) {
    if (GetWiFiStatus() != 1) return;
    char counter = 0;
    while (!client.connected()) {
        Log(LOG, "MQTT connecting to %s...\n", GetMQTTIP().c_str());
        if (client.connect(MQTTid, GetMQTTUser().c_str(), GetMQTTPassword().c_str())) {
            MQTTActive = 1;
            Log(LOG, "MQTT connected\n");
            client.subscribe("Cellular/CellMsg");
            client.subscribe("Cellular/CellCMD");
            MQTTPublishDiscovery();
            MQTTMessageUpdate();
        }
        if (++counter > 2) {
            Log(ERROR, "MQTT connect failed, locking out\n");
            MQTTLockout = 1;
            break;
        }
    }
}

// ── State publish ─────────────────────────────────────────────
void MQTTMessageUpdate() {
    /* Battery */
    client.publish("Cellular/CellVoltage", GetCellVString().c_str());
    client.publish("Cellular/CellPWR",     GetCellSoCString(1).c_str());
    client.publish("Cellular/CellRate",    GetCellRateString().c_str());
    client.publish("Cellular/PowerMode",   GetPowerModeString().c_str());
    client.publish("Cellular/CellAlert",   GetCellAlert() ? "ON" : "OFF");

    /* Cellular */
    client.publish("Cellular/CellStatus",    CellStatString().c_str());
    client.publish("Cellular/CellSignal",    CellSigString().c_str());
    client.publish("Cellular/CellConnected", CellIsConnected() ? "ON" : "OFF");

    /* GPS */
    client.publish("Cellular/GPSFix",     GPShasFix() ? "ON" : "OFF");
    client.publish("Cellular/GPS",        CellGPSString().c_str());
    client.publish("Cellular/GPSLat",     GPSlatString().c_str());
    client.publish("Cellular/GPSLon",     GPSlonString().c_str());
    client.publish("Cellular/GPSSpeed",   GPShasFix() ? String(GPSspeed(),   1).c_str() : "0.0");
    client.publish("Cellular/GPSAlt",     GPShasFix() ? String(GPSaltitude(),1).c_str() : "0.0");
    client.publish("Cellular/GPSHeading", GPShasFix() ? String(GPSheading(), 1).c_str() : "0.0");
}

char GetMQTTStatus(void) { return MQTTActive; }
char GetMQTTMsg()        { return MsgType; }
void CLRMQTTMsg()        { MsgType = 0; }
