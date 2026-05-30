/**
 * @file MQTT.cpp
 * @brief MQTT client with Home Assistant MQTT Discovery auto-configuration.
 *
 * On first connection, 14 retained discovery payloads are published to
 * @c homeassistant/sensor/ and @c homeassistant/binary_sensor/ so Home
 * Assistant auto-creates entities without any manual YAML.
 *
 * Inbound topic: @c Cellular/CellMsg — JSON @c {Title, Loc, Msg}
 *   where Loc=="PO" queues a Pushover notification.
 *
 * Outbound topics: see MQTTMessageUpdate().
 */
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

static char MQTTActive  = 0;
static char MQTTLockout = 0;
static char MsgType     = 0;

static char mqttBrokerIP[16];
static char _haDevId[32]    = "";
static char _haDevJson[180] = "";

static WiFiClient    wclient;
static PubSubClient  client(wclient);
static StaticJsonDocument<200> jsonmqttRx;

/**
 * @brief Publish a retained HA sensor discovery config.
 * @param objId      Unique object ID suffix (e.g. @c "batt_v").
 * @param name       Human-readable entity name.
 * @param stateTopic MQTT state topic.
 * @param unit       Unit of measurement string.
 * @param devClass   HA device class (empty string to omit).
 */
static void haPublishSensor(const char* objId, const char* name,
                             const char* stateTopic, const char* unit,
                             const char* devClass) {
    char topic[128], payload[512];
    snprintf(topic, sizeof(topic), HA_PREFIX "/sensor/%s/%s/config", _haDevId, objId);
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

/**
 * @brief Publish a retained HA binary_sensor discovery config.
 * @param objId      Unique object ID suffix.
 * @param name       Human-readable entity name.
 * @param stateTopic MQTT state topic.
 * @param devClass   HA device class (empty to omit).
 */
static void haPublishBinary(const char* objId, const char* name,
                             const char* stateTopic, const char* devClass) {
    char topic[128], payload[512];
    snprintf(topic, sizeof(topic), HA_PREFIX "/binary_sensor/%s/%s/config", _haDevId, objId);
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

/**
 * @brief Publish a retained HA sensor discovery config for a text value (no unit).
 * @param objId      Unique object ID suffix.
 * @param name       Human-readable entity name.
 * @param stateTopic MQTT state topic.
 */
static void haPublishText(const char* objId, const char* name,
                           const char* stateTopic) {
    char topic[128], payload[512];
    snprintf(topic, sizeof(topic), HA_PREFIX "/sensor/%s/%s/config", _haDevId, objId);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"state_topic\":\"%s\","
        "\"unique_id\":\"%s_%s\",\"device\":%s}",
        name, stateTopic, _haDevId, objId, _haDevJson);
    client.publish(topic, payload, true);
}

/**
 * @brief Publish all 14 Home Assistant discovery payloads.
 *
 * Groups: battery (5), cellular (3), GPS (6).
 * All payloads are retained so HA picks them up after a broker restart.
 */
static void MQTTPublishDiscovery() {
    if (_haDevId[0] == '\0') return;
    Log(NOTIFY, "MQTT: publishing HA discovery\n");

    haPublishSensor("batt_v",    "Battery Voltage",     "Cellular/CellVoltage", "V",   "voltage");
    haPublishSensor("batt_soc",  "Battery SoC",         "Cellular/CellPWR",     "%",   "battery");
    haPublishSensor("batt_rate", "Battery Charge Rate", "Cellular/CellRate",    "%/h", "");
    haPublishText  ("pwr_src",   "Power Source",        "Cellular/PowerMode");
    haPublishBinary("batt_alert","Battery Alert",       "Cellular/CellAlert",   "battery");

    haPublishSensor("cell_rssi", "Cell RSSI",     "Cellular/CellSignal",    "dBm", "signal_strength");
    haPublishText  ("cell_stat", "Cell Status",   "Cellular/CellStatus");
    haPublishBinary("cell_conn", "Cell Connected","Cellular/CellConnected", "connectivity");

    haPublishBinary("gps_fix",  "GPS Fix",       "Cellular/GPSFix",     "");
    haPublishSensor("gps_lat",  "GPS Latitude",  "Cellular/GPSLat",     "°",    "");
    haPublishSensor("gps_lon",  "GPS Longitude", "Cellular/GPSLon",     "°",    "");
    haPublishSensor("gps_spd",  "GPS Speed",     "Cellular/GPSSpeed",   "km/h", "speed");
    haPublishSensor("gps_alt",  "GPS Altitude",  "Cellular/GPSAlt",     "m",    "distance");
    haPublishSensor("gps_hdg",  "GPS Heading",   "Cellular/GPSHeading", "°",    "");

    Log(NOTIFY, "MQTT: HA discovery complete (14 entities)\n");
}

/**
 * @brief Handle inbound MQTT messages.
 *
 * @c Cellular/CellMsg with JSON @c {Loc:"PO"} queues a Pushover notification.
 */
static void callback(char* topic, byte* payload, unsigned int length) {
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

/**
 * @brief Service the MQTT client.
 *
 * When connected, calls client.loop().  When disconnected, calls MQTTreconnect().
 * A 2-minute lockout prevents rapid reconnect storms after repeated failures.
 */
void MqttLoop(void) {
    if (MQTTLockout) {
        static unsigned long lockoutStart = 0;
        if (lockoutStart == 0) lockoutStart = millis();
        if (millis() - lockoutStart > 120000UL) { MQTTLockout = 0; lockoutStart = 0; }
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

/**
 * @brief Configure MQTT server, buffer size, and build device metadata strings.
 *
 * Device ID is the unique name lowercased with hyphens replaced by underscores.
 */
void MQTTStart() {
    String un = GetUniqueName();
    String id = un; id.toLowerCase(); id.replace("-", "_");
    strlcpy(_haDevId, id.c_str(), sizeof(_haDevId));
    snprintf(_haDevJson, sizeof(_haDevJson),
        "{\"identifiers\":[\"%s\"],\"name\":\"%s\","
        "\"manufacturer\":\"Custom\",\"model\":\"ESP32-LTE-SIM7000\"}",
        _haDevId, un.c_str());
    strlcpy(mqttBrokerIP, GetMQTTIP().c_str(), sizeof(mqttBrokerIP));
    client.setBufferSize(512);
    client.setServer(mqttBrokerIP, GetMQTTPort());
    client.setKeepAlive(60);
    client.setCallback(callback);
}

/**
 * @brief Attempt to reconnect to the broker (up to 3 tries).
 *
 * On success, subscribes to command topics, publishes HA discovery, and
 * sends an initial state update.  On persistent failure, sets MQTTLockout.
 */
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

/**
 * @brief Publish all sensor values to their MQTT topics.
 *
 * Topics: CellVoltage, CellPWR, CellRate, PowerMode, CellAlert,
 * CellStatus, CellSignal, CellConnected, GPSFix, GPS, GPSLat, GPSLon,
 * GPSSpeed, GPSAlt, GPSHeading (all under the @c Cellular/ prefix).
 */
void MQTTMessageUpdate() {
    client.publish("Cellular/CellVoltage", GetCellVString().c_str());
    client.publish("Cellular/CellPWR",     GetCellSoCString(1).c_str());
    client.publish("Cellular/CellRate",    GetCellRateString().c_str());
    client.publish("Cellular/PowerMode",   GetPowerModeString().c_str());
    client.publish("Cellular/CellAlert",   GetCellAlert() ? "ON" : "OFF");

    client.publish("Cellular/CellStatus",    CellStatString().c_str());
    client.publish("Cellular/CellSignal",    CellSigString().c_str());
    client.publish("Cellular/CellConnected", CellIsConnected() ? "ON" : "OFF");

    client.publish("Cellular/GPSFix",     GPShasFix() ? "ON" : "OFF");
    client.publish("Cellular/GPS",        CellGPSString().c_str());
    client.publish("Cellular/GPSLat",     GPSlatString().c_str());
    client.publish("Cellular/GPSLon",     GPSlonString().c_str());
    client.publish("Cellular/GPSSpeed",   GPShasFix() ? String(GPSspeed(),   1).c_str() : "0.0");
    client.publish("Cellular/GPSAlt",     GPShasFix() ? String(GPSaltitude(),1).c_str() : "0.0");
    client.publish("Cellular/GPSHeading", GPShasFix() ? String(GPSheading(), 1).c_str() : "0.0");
}

/** @return @c 1 when broker is connected, @c 0 otherwise. */
char GetMQTTStatus(void) { return MQTTActive; }

/** @return Current inbound message type code. */
char GetMQTTMsg()        { return MsgType; }

/** @brief Clear the inbound message type flag. */
void CLRMQTTMsg()        { MsgType = 0; }
