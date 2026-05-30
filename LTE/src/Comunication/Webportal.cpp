/**
 * @file Webportal.cpp
 * @brief ESPAsyncWebServer — REST API, config endpoints, and WebSocket log stream.
 *
 * The web portal serves @c Main.html from LittleFS on port 80 and exposes
 * a full REST API under @c /api/ for live telemetry and control, plus
 * configuration endpoints under @c /config/.  Log messages are pushed to
 * connected browsers via a WebSocket at @c /ws in real time.
 *
 * A pending Pushover test is deferred to WebHandel() so the HTTP response
 * is returned before the (potentially blocking) cellular job is queued.
 */
#include <ArduinoJson.h>
#include "FS.h"
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FileSystem/FSInterface.h"
#include "FileSystem/FileSystem.h"
#include "Wifi.h"
#include "Define.h"
#include "Hardware/Log.h"
#include "Hardware/RS485.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"
#include "Functions.h"
#include "Comunication/MQTT.h"
#include <WiFi.h>

#define HTTP_PORT 80

static AsyncWebServer server(HTTP_PORT);
static AsyncWebSocket ws("/ws");

static char     output[768];
static bool     wsconnected      = false;
static bool     firstUpdate      = false;
static char     jsonType         = 1;
static unsigned long lastWebTime = 0;
static int      webInterval      = 500;
static bool     pendingPushoverTest = false;

/* ── WebSocket helpers ──────────────────────────────────────────────────── */

/**
 * @brief Serialise a JSON document and broadcast it to all WebSocket clients.
 * @param doc JSON document to send.
 */
static void wsSend(JsonDocument& doc) {
    serializeJson(doc, output, sizeof(output));
    if (ws.availableForWriteAll()) ws.textAll(output);
}

/**
 * @brief Push a log string to all connected WebSocket clients.
 * @param msg Log message string.
 * @return @c 1 if sent; @c 0 if no client is connected.
 */
char WebLogSend(String msg) {
    if (!wsconnected) return 0;
    StaticJsonDocument<256> doc;
    doc["Type"] = 10;
    doc["LOG"]  = msg;
    wsSend(doc);
    return 1;
}

/**
 * @brief Handle WebSocket connect/disconnect events.
 *
 * On connect, sends a greeting JSON with device name, IP, hostname, and MAC.
 */
static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        wsconnected = true;
        firstUpdate  = true;
        StaticJsonDocument<256> doc;
        doc["Type"] = 0;
        doc["SSID"] = GetSSID();
        doc["IP"]   = GetIP();
        doc["HN"]   = GetUniqueName();
        doc["MAC"]  = GetMAC();
        wsSend(doc);
        client->ping();
    } else if (type == WS_EVT_DISCONNECT) {
        wsconnected = false;
    }
}

/* ── JSON response helper ───────────────────────────────────────────────── */

/**
 * @brief Serialise a JSON document and send it as an HTTP 200 JSON response.
 * @param req HTTP request object.
 * @param doc JSON document to serialise.
 */
static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

/* ── GET API handlers ───────────────────────────────────────────────────── */

/** @brief GET /api/status — device summary (name, IP, WiFi/cell/GPS, battery). */
static void handleApiStatus(AsyncWebServerRequest* req) {
    StaticJsonDocument<384> doc;
    doc["name"]   = GetUniqueName();
    doc["ip"]     = GetIP();
    doc["mac"]    = GetMAC();
    doc["wrssi"]  = WiFi.RSSI();
    doc["soc"]    = (int)GetCellSoC();
    doc["crssi"]  = (int)CellSig();
    doc["gfix"]   = GPShasFix();
    doc["rserr"]  = 0;
    doc["wifi"]   = (GetWiFiStatus() == 1);
    doc["mqtt"]   = (GetMQTTStatus() == 1);
    doc["cell"]   = CellIsConnected();
    doc["hbsecs"] = CellNextHeartbeatSecs();
    sendJson(req, doc);
}

/** @brief GET /api/cellular — LTE modem status, RSSI, SIM, state. */
static void handleApiCellular(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["status"]    = CellStatString();
    doc["state"]     = CellStateStr();
    doc["network"]   = CellNetworkString();
    doc["ip"]        = CellIPString();
    doc["sim"]       = CellSIMString();
    doc["rssi"]      = (int)CellSig();
    doc["connected"] = CellIsConnected();
    doc["on"]        = CellIsOn();
    sendJson(req, doc);
}

/** @brief GET /api/gps — GPS fix status and position. */
static void handleApiGPS(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    bool fix = GPShasFix();
    doc["fix"]     = fix;
    doc["enabled"] = GPSisEnabled();
    doc["lat"]     = fix ? GPSlat()      : 0.0f;
    doc["lon"]     = fix ? GPSlon()      : 0.0f;
    doc["alt"]     = fix ? GPSaltitude() : 0.0f;
    doc["speed"]   = fix ? GPSspeed()    : 0.0f;
    doc["heading"] = fix ? GPSheading()  : 0.0f;
    sendJson(req, doc);
}

/**
 * @brief POST /api/gps/enable — enable or disable the GNSS receiver.
 * @param data Request body JSON: @c {"enable": true|false}.
 */
static void handleApiGPSEnable(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok)
        GPSenable(body["enable"] | false);
    req->send(200, "application/json", "{\"ok\":true}");
}

/** @brief GET /api/battery — voltage, SoC, charge rate, power mode, alert. */
static void handleApiBattery(AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["soc"]   = GetCellSoC();
    doc["volt"]  = GetCellV();
    doc["rate"]  = GetCellRate();
    doc["mode"]  = GetPowerModeString();
    doc["alert"] = GetCellAlert();
    sendJson(req, doc);
}

/** @brief GET /api/rs485 — Modbus slave ID, message count, RS-485 mode flag. */
static void handleApiRS485(AsyncWebServerRequest* req) {
    StaticJsonDocument<96> doc;
    doc["slaveId"]    = RS485slaveId();
    doc["errorCount"] = 0;
    doc["msgCount"]   = RS485getMsgCount();
    doc["rs485Mode"]  = GetRS485Mode();
    sendJson(req, doc);
}

/** @brief GET /api/rs485/events — newline-delimited event ring-buffer. */
static void handleApiRS485Events(AsyncWebServerRequest* req) {
    req->send(200, "text/plain", RS485getEvents());
}

/**
 * @brief POST /api/rs485/mode — toggle RS-485 mode and reboot.
 * @param data Request body JSON: @c {"enable": true|false}.
 */
static void handleApiRS485Mode(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok)
        SetRS485Mode(body["enable"] | false);
    req->send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

/**
 * @brief POST /api/debug/serial — enable or disable UART0 serial debug output.
 * @param data Request body JSON: @c {"enable": true|false}.
 */
static void handleApiDebugSerial(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok)
        SetSerialLog(body["enable"] | false);
    req->send(200, "application/json", "{\"ok\":true}");
}

/** @brief POST /api/reboot — reboot the device after sending the response. */
static void handleApiReboot(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

/* ── Config: Pushover ───────────────────────────────────────────────────── */

/** @brief GET /config/pushover — return current Pushover credentials. */
static void handleGetPushover(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["enabled"] = (GetPushoverEnabled() == 1);
    doc["token"]   = GetPushoverToken();
    doc["userKey"] = GetPushoverUserKey();
    sendJson(req, doc);
}

/**
 * @brief POST /config/pushover — save new Pushover credentials.
 * @param data Request body JSON: @c {"enabled", "token", "userKey"}.
 */
static void handlePostPushover(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<256> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    unsigned char enabled = body["enabled"] | GetPushoverEnabled();
    String token   = body["token"]   | GetPushoverToken();
    String userKey = body["userKey"] | GetPushoverUserKey();
    SetPushoverConfig(enabled, token.c_str(), userKey.c_str());
    req->send(200, "application/json", "{\"ok\":true}");
}

/**
 * @brief POST /api/pushover/test — queue a test Pushover notification.
 *
 * The actual Pushover() call is deferred to WebHandel() so this handler
 * can return immediately.
 */
static void handleTestPushover(AsyncWebServerRequest* req) {
    pendingPushoverTest = true;
    req->send(200, "application/json", "{\"ok\":true}");
}

/** @brief GET /config/cellular — return current heartbeat interval in minutes. */
static void handleGetCellConfig(AsyncWebServerRequest* req) {
    StaticJsonDocument<64> doc;
    doc["heartbeatMins"] = GetHeartbeatMins();
    sendJson(req, doc);
}

/**
 * @brief POST /config/cellular — update the heartbeat interval.
 * @param data Request body JSON: @c {"heartbeatMins": <uint>}.
 */
static void handlePostCellConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    unsigned int mins = body["heartbeatMins"] | GetHeartbeatMins();
    if (mins < 1) mins = 1;
    SetHeartbeatMins(mins);
    req->send(200, "application/json", "{\"ok\":true}");
}

/** @brief GET /api/pushover/status — last Pushover title, success flag, and status string. */
static void handleApiPushoverStatus(AsyncWebServerRequest* req) {
    char title[48], status[40];
    bool ok = false;
    CellLastPushover(title, &ok, status);
    StaticJsonDocument<160> doc;
    doc["title"]  = title;
    doc["ok"]     = ok;
    doc["status"] = status;
    sendJson(req, doc);
}

/* ── Config: WiFi ───────────────────────────────────────────────────────── */

/** @brief GET /config/wifi — return current WiFi SSID and hostname. */
static void handleGetWifi(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["ssid"] = GetSSID();
    doc["host"] = GetHostName();
    doc["dhcp"] = true;
    doc["ip"]   = "";
    doc["gw"]   = "";
    doc["sub"]  = "";
    sendJson(req, doc);
}

/**
 * @brief POST /config/wifi — save new WiFi credentials and reboot.
 * @param data Request body JSON: @c {"ssid", "pass", "host", "dhcp"}.
 */
static void handlePostWifi(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<256> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    extern WiFiConfig wfconfig;
    strlcpy(wfconfig.SSID,     body["ssid"] | wfconfig.SSID,     sizeof(wfconfig.SSID));
    strlcpy(wfconfig.Passcode, body["pass"] | wfconfig.Passcode, sizeof(wfconfig.Passcode));
    strlcpy(wfconfig.Host,     body["host"] | wfconfig.Host,     sizeof(wfconfig.Host));
    wfconfig.DHCP    = body["dhcp"] | 1;
    wfconfig.SSIDLN  = strlen(wfconfig.SSID);
    wfconfig.PswdLN  = strlen(wfconfig.Passcode);
    wfconfig.HoastLN = strlen(wfconfig.Host);
    extern void WifisaveConfiguration(WiFiConfig*);
    WifisaveConfiguration(&wfconfig);
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

/* ── Config: MQTT ───────────────────────────────────────────────────────── */

/** @brief GET /config/mqtt — return current MQTT broker settings. */
static void handleGetMqtt(AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["enabled"] = (GetMQTTEnabled() == 1);
    doc["ip"]      = GetMQTTIP();
    doc["port"]    = GetMQTTPort();
    doc["user"]    = GetMQTTUser();
    sendJson(req, doc);
}

/**
 * @brief POST /config/mqtt — save new MQTT broker settings and reboot.
 * @param data Request body JSON: @c {"enabled", "ip", "port", "user", "pass"}.
 */
static void handlePostMqtt(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<256> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    extern MQTTConfig mqconfig;
    mqconfig.MQTTEnabble = body["enabled"] | mqconfig.MQTTEnabble;
    mqconfig.MQTTPort    = body["port"]    | mqconfig.MQTTPort;
    strlcpy(mqconfig.MQTTIP,       body["ip"]   | mqconfig.MQTTIP,       sizeof(mqconfig.MQTTIP));
    strlcpy(mqconfig.MQTTUser,     body["user"] | mqconfig.MQTTUser,     sizeof(mqconfig.MQTTUser));
    strlcpy(mqconfig.MQTTPassword, body["pass"] | mqconfig.MQTTPassword, sizeof(mqconfig.MQTTPassword));
    mqconfig.MQTTPasswordLN = strlen(mqconfig.MQTTPassword);
    extern void MqttsaveConfiguration(MQTTConfig*);
    MqttsaveConfiguration(&mqconfig);
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

/* ── Server init ────────────────────────────────────────────────────────── */

/**
 * @brief Register all HTTP routes, attach the WebSocket, and start the server.
 *
 * All static assets under @c / are served from LittleFS via @c serveStatic().
 */
void WebStart() {
    Log(NOTIFY, "Web service start\n");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/Main.html", "text/html");
    });

    server.on("/api/status",   HTTP_GET, handleApiStatus);
    server.on("/api/cellular", HTTP_GET, handleApiCellular);
    server.on("/api/gps",      HTTP_GET, handleApiGPS);
    server.on("/api/battery",  HTTP_GET, handleApiBattery);
    server.on("/api/rs485",        HTTP_GET, handleApiRS485);
    server.on("/api/rs485/events", HTTP_GET, handleApiRS485Events);
    server.on("/api/reboot",   HTTP_POST, handleApiReboot);

    server.on("/api/gps/enable", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiGPSEnable);
    server.on("/api/debug/serial", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiDebugSerial);
    server.on("/api/rs485/mode", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiRS485Mode);

    server.on("/config/wifi", HTTP_GET, handleGetWifi);
    server.on("/config/wifi", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handlePostWifi);
    server.on("/config/mqtt", HTTP_GET, handleGetMqtt);
    server.on("/config/mqtt", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handlePostMqtt);
    server.on("/config/pushover", HTTP_GET, handleGetPushover);
    server.on("/config/pushover", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handlePostPushover);
    server.on("/api/pushover/test",   HTTP_POST, handleTestPushover);
    server.on("/config/cellular", HTTP_GET, handleGetCellConfig);
    server.on("/config/cellular", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handlePostCellConfig);
    server.on("/api/pushover/status", HTTP_GET, handleApiPushoverStatus);

    server.serveStatic("/", LittleFS, "/");
    server.onNotFound([](AsyncWebServerRequest* req){ req->send(404); });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
}

/**
 * @brief Service periodic web portal tasks.
 *
 * Sends any deferred Pushover test notification, then cleans up stale
 * WebSocket clients every @c webInterval ms.
 */
void WebHandel() {
    if (pendingPushoverTest) {
        pendingPushoverTest = false;
        Pushover("LTE Test", "Test notification from ESP32 LTE device");
    }
    if (!wsconnected) return;
    if ((millis() - lastWebTime) < (unsigned long)webInterval) return;
    lastWebTime = millis();
    ws.cleanupClients();
}
