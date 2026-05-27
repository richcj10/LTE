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

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");

static char     output[768];
static bool     wsconnected = false;
static bool     firstUpdate = false;
static char     jsonType    = 1;
static unsigned long lastWebTime = 0;
static int      webInterval = 500;
static bool     pendingPushoverTest = false;

/* ── WebSocket ──────────────────────────────────────────── */
void wsSend(JsonDocument& doc) {
    serializeJson(doc, output, sizeof(output));
    if (ws.availableForWriteAll()) ws.textAll(output);
}

char WebLogSend(String msg) {
    if (!wsconnected) return 0;
    StaticJsonDocument<256> doc;
    doc["Type"] = 10;
    doc["LOG"]  = msg;
    wsSend(doc);
    return 1;
}

void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
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

/* ── JSON helpers ───────────────────────────────────────── */
static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

/* ── API handlers ───────────────────────────────────────── */
void handleApiStatus(AsyncWebServerRequest* req) {
    StaticJsonDocument<384> doc;
    doc["name"]  = GetUniqueName();
    doc["ip"]    = GetIP();
    doc["mac"]   = GetMAC();
    doc["wrssi"] = WiFi.RSSI();
    doc["soc"]   = (int)GetCellSoC();
    doc["crssi"] = (int)CellSig();
    doc["gfix"]  = GPShasFix();
    doc["rserr"] = 0;
    doc["wifi"]  = (GetWiFiStatus() == 1);
    doc["mqtt"]  = (GetMQTTStatus() == 1);
    doc["cell"]  = CellIsConnected();
    sendJson(req, doc);
}

void handleApiCellular(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["status"]     = CellStatString();
    doc["state"]      = CellStateStr();
    doc["network"]    = CellNetworkString();
    doc["ip"]         = CellIPString();
    doc["sim"]        = CellSIMString();
    doc["rssi"]       = (int)CellSig();
    doc["connected"]  = CellIsConnected();
    doc["on"]         = CellIsOn();
    sendJson(req, doc);
}

void handleApiGPS(AsyncWebServerRequest* req) {
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

void handleApiGPSEnable(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok) {
        GPSenable(body["enable"] | false);
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

void handleApiBattery(AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["soc"]   = GetCellSoC();
    doc["volt"]  = GetCellV();
    doc["rate"]  = GetCellRate();
    doc["mode"]  = GetPowerModeString();
    doc["alert"] = GetCellAlert();
    sendJson(req, doc);
}

void handleApiRS485(AsyncWebServerRequest* req) {
    StaticJsonDocument<96> doc;
    doc["slaveId"]    = RS485slaveId();
    doc["errorCount"] = 0;
    doc["msgCount"]   = RS485getMsgCount();
    doc["rs485Mode"]  = GetRS485Mode();
    sendJson(req, doc);
}

void handleApiRS485Events(AsyncWebServerRequest* req) {
    req->send(200, "text/plain", RS485getEvents());
}

void handleApiRS485Mode(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok)
        SetRS485Mode(body["enable"] | false);
    req->send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

void handleApiDebugSerial(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<64> body;
    if (deserializeJson(body, data, len) == DeserializationError::Ok) {
        SetSerialLog(body["enable"] | false);
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

void handleApiReboot(AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

/* ── Config: Pushover ───────────────────────────────────── */
void handleGetPushover(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["enabled"] = (GetPushoverEnabled() == 1);
    doc["token"]   = GetPushoverToken();
    doc["userKey"] = GetPushoverUserKey();
    sendJson(req, doc);
}

void handlePostPushover(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
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

void handleTestPushover(AsyncWebServerRequest* req) {
    pendingPushoverTest = true;
    req->send(200, "application/json", "{\"ok\":true}");
}

void handleGetCellConfig(AsyncWebServerRequest* req) {
    StaticJsonDocument<64> doc;
    doc["heartbeatMins"] = GetHeartbeatMins();
    sendJson(req, doc);
}

void handlePostCellConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
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

void handleApiPushoverStatus(AsyncWebServerRequest* req) {
    char title[48], status[40];
    bool ok = false;
    CellLastPushover(title, &ok, status);
    StaticJsonDocument<160> doc;
    doc["title"]  = title;
    doc["ok"]     = ok;
    doc["status"] = status;
    sendJson(req, doc);
}

/* ── Config: WiFi ───────────────────────────────────────── */
void handleGetWifi(AsyncWebServerRequest* req) {
    StaticJsonDocument<256> doc;
    doc["ssid"] = GetSSID();
    doc["host"] = GetHostName();
    doc["dhcp"] = true;
    doc["ip"]   = "";
    doc["gw"]   = "";
    doc["sub"]  = "";
    sendJson(req, doc);
}

void handlePostWifi(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<256> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    extern WiFiConfig wfconfig;
    strlcpy(wfconfig.SSID,    body["ssid"] | wfconfig.SSID,    sizeof(wfconfig.SSID));
    strlcpy(wfconfig.Passcode,body["pass"] | wfconfig.Passcode,sizeof(wfconfig.Passcode));
    strlcpy(wfconfig.Host,    body["host"] | wfconfig.Host,    sizeof(wfconfig.Host));
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

/* ── Config: MQTT ───────────────────────────────────────── */
void handleGetMqtt(AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["enabled"] = (GetMQTTEnabled() == 1);
    doc["ip"]      = GetMQTTIP();
    doc["port"]    = GetMQTTPort();
    doc["user"]    = GetMQTTUser();
    sendJson(req, doc);
}

void handlePostMqtt(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    StaticJsonDocument<256> body;
    if (deserializeJson(body, data, len) != DeserializationError::Ok) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
        return;
    }
    extern MQTTConfig mqconfig;
    mqconfig.MQTTEnabble = body["enabled"] | mqconfig.MQTTEnabble;
    mqconfig.MQTTPort    = body["port"]    | mqconfig.MQTTPort;
    strlcpy(mqconfig.MQTTIP,       body["ip"]   | mqconfig.MQTTIP,      sizeof(mqconfig.MQTTIP));
    strlcpy(mqconfig.MQTTUser,     body["user"] | mqconfig.MQTTUser,    sizeof(mqconfig.MQTTUser));
    strlcpy(mqconfig.MQTTPassword, body["pass"] | mqconfig.MQTTPassword,sizeof(mqconfig.MQTTPassword));
    mqconfig.MQTTPasswordLN = strlen(mqconfig.MQTTPassword);
    extern void MqttsaveConfiguration(MQTTConfig*);
    MqttsaveConfiguration(&mqconfig);
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
}

/* ── Server init ────────────────────────────────────────── */
void WebStart() {
    Log(NOTIFY, "Web service start\n");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/Main.html", "text/html");
    });

    /* API endpoints */
    server.on("/api/status",   HTTP_GET,  handleApiStatus);
    server.on("/api/cellular", HTTP_GET,  handleApiCellular);
    server.on("/api/gps",      HTTP_GET,  handleApiGPS);
    server.on("/api/battery",  HTTP_GET,  handleApiBattery);
    server.on("/api/rs485",        HTTP_GET, handleApiRS485);
    server.on("/api/rs485/events", HTTP_GET, handleApiRS485Events);
    server.on("/api/reboot",   HTTP_POST, handleApiReboot);

    server.on("/api/gps/enable", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiGPSEnable);

    server.on("/api/debug/serial", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiDebugSerial);

    server.on("/api/rs485/mode", HTTP_POST, [](AsyncWebServerRequest* req){},
        NULL, handleApiRS485Mode);

    /* Config endpoints */
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
    server.on("/api/pushover/status", HTTP_GET,  handleApiPushoverStatus);

    server.serveStatic("/", LittleFS, "/");
    server.onNotFound([](AsyncWebServerRequest* req){ req->send(404); });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
}

/* ── Periodic WebSocket push (logs already sent via WebLogSend) ─────── */
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
