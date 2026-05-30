/**
 * @file FSInterface.cpp
 * @brief Config facade — loads all structs from FileSystem and exposes typed accessors.
 *
 * All config structs live here as module-level variables.  FSInterface.h
 * functions are the only public interface; the structs are not exported.
 */
#include "FSInterface.h"
#include "FileSystem.h"
#include "Arduino.h"

static WiFiConfig     wfconfig;
static MQTTConfig     mqconfig;
static SystemConfig   sysconfig;
static PushoverConfig pvconfig;
static CellularConfig cellconfig;

static bool _needsAP = false;

/**
 * @brief Mount LittleFS and load all configuration structs.
 * @return @c 1 = OK, @c 2 = no WiFi creds (AP mode needed), @c 0 = FS error.
 */
char FileStstemStart() {
    char result = FileSystemInit(&wfconfig, &mqconfig, &sysconfig, &pvconfig);
    CellularComfig(&cellconfig);
    _needsAP = (result == 2);
    return (result == 0) ? 0 : 1;
}

/** @return @c true if AP mode is required (no WiFi credentials found). */
bool NeedsAPMode() { return _needsAP; }

/** @return WiFi mode byte. */
unsigned char GetWiFiMode() { return wfconfig.WIFIMode; }

/** @return Stored SSID string. */
String GetSSID() {
    char ReturnArray[wfconfig.SSIDLN + 1];
    for (unsigned char i = 0; i <= wfconfig.SSIDLN; i++)
        ReturnArray[i] = wfconfig.SSID[i];
    return String(ReturnArray);
}

/** @return Stored WiFi passphrase. */
String GetSSIDPassword() {
    char ReturnArray[wfconfig.PswdLN + 1];
    for (unsigned char i = 0; i <= wfconfig.PswdLN; i++)
        ReturnArray[i] = wfconfig.Passcode[i];
    return String(ReturnArray);
}

/** @return Device mDNS hostname. */
String GetHostName() {
    char ReturnArray[wfconfig.HoastLN + 1];
    for (unsigned char i = 0; i <= wfconfig.HoastLN; i++)
        ReturnArray[i] = wfconfig.Host[i];
    return String(ReturnArray);
}

unsigned char GetMQTTEnabled()  { return mqconfig.MQTTEnabble; }
String        GetMQTTIP()       { return String(mqconfig.MQTTIP); }
unsigned int  GetMQTTPort()     { return mqconfig.MQTTPort; }
String        GetMQTTUser()     { return String(mqconfig.MQTTUser); }
String        GetMQTTPassword() { return String(mqconfig.MQTTPassword); }

/** @return @c true when RS-485/Modbus mode is active. */
bool GetRS485Mode() { return sysconfig.rs485Mode; }

/**
 * @brief Enable or disable RS-485 mode and persist the change.
 * @param on @c true to activate RS-485; @c false for serial debug.
 */
void SetRS485Mode(bool on) {
    sysconfig.rs485Mode = on;
    SystemsaveConfiguration(&sysconfig);
}

unsigned char GetPushoverEnabled()  { return pvconfig.enabled; }
String        GetPushoverToken()    { return String(pvconfig.token); }
String        GetPushoverUserKey()  { return String(pvconfig.userKey); }

/**
 * @brief Update and persist Pushover credentials.
 * @param enabled @c 1 to enable, @c 0 to disable.
 * @param token   Application token (max 47 chars).
 * @param userKey User or group key (max 47 chars).
 */
void SetPushoverConfig(unsigned char enabled, const char* token, const char* userKey) {
    pvconfig.enabled = enabled;
    strlcpy(pvconfig.token,   token,   sizeof(pvconfig.token));
    strlcpy(pvconfig.userKey, userKey, sizeof(pvconfig.userKey));
    PushoversaveConfiguration(&pvconfig);
}

/** @return Heartbeat interval in minutes. */
unsigned int GetHeartbeatMins() { return cellconfig.heartbeatMins; }

/**
 * @brief Update the heartbeat interval and persist it.
 * @param mins New interval (minimum 1 minute).
 */
void SetHeartbeatMins(unsigned int mins) {
    cellconfig.heartbeatMins = mins;
    CellularsaveConfiguration(&cellconfig);
}

/**
 * @brief Save new WiFi credentials to NVS and clear the AP-mode flag.
 */
void SaveWifiToNVS(const char* ssid, const char* pass, const char* host) {
    strlcpy(wfconfig.SSID,     ssid, sizeof(wfconfig.SSID));
    wfconfig.SSIDLN = strlen(ssid);
    strlcpy(wfconfig.Passcode, pass, sizeof(wfconfig.Passcode));
    wfconfig.PswdLN = strlen(pass);
    strlcpy(wfconfig.Host,     host, sizeof(wfconfig.Host));
    wfconfig.HoastLN = strlen(host);
    _needsAP = false;
    NVSsaveWifi(&wfconfig);
}

/**
 * @brief Save new MQTT broker settings to NVS.
 */
void SaveMQTTToNVS(const char* ip, unsigned int port, const char* user, const char* pass, unsigned char enabled) {
    strlcpy(mqconfig.MQTTIP,       ip,   sizeof(mqconfig.MQTTIP));
    mqconfig.MQTTPort       = port;
    mqconfig.MQTTEnabble    = enabled;
    strlcpy(mqconfig.MQTTUser,     user, sizeof(mqconfig.MQTTUser));
    strlcpy(mqconfig.MQTTPassword, pass, sizeof(mqconfig.MQTTPassword));
    mqconfig.MQTTPasswordLN = strlen(pass);
    NVSsaveMQTT(&mqconfig);
}
