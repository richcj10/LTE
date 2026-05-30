/**
 * @file FileSystem.cpp
 * @brief LittleFS mount, NVS read/write, and JSON config load/save for all subsystems.
 *
 * Config loading priority:
 *   WiFi:     NVS → JSON file → signal AP-mode needed (return 2)
 *   MQTT:     NVS → JSON file → built-in struct defaults
 *   Pushover: JSON file → built-in defaults
 *   System:   JSON file → built-in defaults
 *   Cellular: JSON file → built-in defaults
 */
#include "FileSystem.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Define.h"
#include "Functions.h"
#include "FS.h"
#include <LittleFS.h>
#include <time.h>
#include "Hardware/Log.h"

#define FORMAT_LITTLEFS_IF_FAILED true

static const char* WiFifilename      = "/WiFiconfig.json";
static const char* MQTTfilename      = "/MQTTconfig.json";
static const char* Systemfilename    = "/system.json";
static const char* Pushoverfilename  = "/Pushoverconfig.json";
static const char* Cellularfilename  = "/Cellularconfig.json";

static bool DefaultsLoaded = 0;

static void listDir(fs::FS& fs, const char* dirname, uint8_t levels);
static void writeFile(fs::FS& fs, const char* path, const char* message);

/* ── NVS helpers ─────────────────────────────────────────────────────────── */

/**
 * @brief Load WiFi credentials from NVS (Preferences namespace "wifi").
 * @return @c true if an SSID was found.
 */
bool NVSloadWifi(struct WiFiConfig* WFC) {
  Preferences p;
  p.begin("wifi", true);
  String ssid = p.getString("ssid", "");
  p.end();
  if (ssid.length() == 0) return false;
  p.begin("wifi", true);
  strlcpy(WFC->SSID,    ssid.c_str(), sizeof(WFC->SSID));  WFC->SSIDLN = ssid.length();
  String pass = p.getString("pass", "");
  strlcpy(WFC->Passcode, pass.c_str(), sizeof(WFC->Passcode)); WFC->PswdLN = pass.length();
  String host = p.getString("host", WFC->Host);
  strlcpy(WFC->Host,    host.c_str(), sizeof(WFC->Host));   WFC->HoastLN = host.length();
  WFC->DHCP     = p.getUChar("dhcp", WFC->DHCP);
  WFC->WIFIMode = p.getUChar("mode", WFC->WIFIMode);
  p.end();
  return true;
}

/** @brief Save WiFi credentials to NVS. */
void NVSsaveWifi(struct WiFiConfig* WFC) {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", WFC->SSID);
  p.putString("pass", WFC->Passcode);
  p.putString("host", WFC->Host);
  p.putUChar("dhcp",  WFC->DHCP);
  p.putUChar("mode",  WFC->WIFIMode);
  p.end();
  Log(LOG, "NVS: WiFi saved (ssid=%s)\n", WFC->SSID);
}

/**
 * @brief Load MQTT config from NVS (Preferences namespace "mqtt").
 * @return @c true if a broker IP was found.
 */
bool NVSloadMQTT(struct MQTTConfig* MQC) {
  Preferences p;
  p.begin("mqtt", true);
  String ip = p.getString("ip", "");
  p.end();
  if (ip.length() == 0) return false;
  p.begin("mqtt", true);
  strlcpy(MQC->MQTTIP, ip.c_str(), sizeof(MQC->MQTTIP));
  MQC->MQTTPort    = p.getUInt("port",  MQC->MQTTPort);
  MQC->MQTTEnabble = p.getUChar("en",   MQC->MQTTEnabble);
  String user = p.getString("user", "");
  strlcpy(MQC->MQTTUser,     user.c_str(), sizeof(MQC->MQTTUser));
  String pass = p.getString("pass", "");
  strlcpy(MQC->MQTTPassword, pass.c_str(), sizeof(MQC->MQTTPassword));
  MQC->MQTTPasswordLN = pass.length();
  p.end();
  return true;
}

/** @brief Save MQTT config to NVS. */
void NVSsaveMQTT(struct MQTTConfig* MQC) {
  Preferences p;
  p.begin("mqtt", false);
  p.putString("ip",   MQC->MQTTIP);
  p.putUInt("port",   MQC->MQTTPort);
  p.putUChar("en",    MQC->MQTTEnabble);
  p.putString("user", MQC->MQTTUser);
  p.putString("pass", MQC->MQTTPassword);
  p.end();
  Log(LOG, "NVS: MQTT saved (ip=%s port=%d)\n", MQC->MQTTIP, MQC->MQTTPort);
}

/* ── LittleFS boot ───────────────────────────────────────────────────────── */

/**
 * @brief Mount LittleFS and load all config structs.
 *
 * WiFi priority: NVS → JSON → AP mode (return 2).
 * MQTT priority: NVS → JSON → struct defaults.
 * Pushover and System always use JSON files.
 *
 * @return @c 1 OK, @c 2 no WiFi creds, @c 0 FS mount failed.
 */
char FileSystemInit(struct WiFiConfig* WFC, struct MQTTConfig* MQC, struct SystemConfig* SC, struct PushoverConfig* PVC) {
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Log(ERROR, "LittleFS Mount Failed\n");
    return 0;
  }
  listDir(LittleFS, "/", 0);

  if (NVSloadWifi(WFC)) {
    Log(NOTIFY, "WiFi: config from NVS (ssid=%s)\n", WFC->SSID);
  } else if (LittleFS.exists(WiFifilename)) {
    Log(NOTIFY, "WiFi: importing JSON → NVS\n");
    WifiloadConfiguration(WFC);
    NVSsaveWifi(WFC);
  } else {
    Log(NOTIFY, "WiFi: no config found — AP mode needed\n");
    PushoverComfig(PVC);
    SystemloadConfiguration(SC);
    return 2;
  }

  if (NVSloadMQTT(MQC)) {
    Log(NOTIFY, "MQTT: config from NVS (ip=%s)\n", MQC->MQTTIP);
  } else if (LittleFS.exists(MQTTfilename)) {
    Log(NOTIFY, "MQTT: importing JSON → NVS\n");
    MqttloadConfiguration(MQC);
    NVSsaveMQTT(MQC);
  } else {
    Log(NOTIFY, "MQTT: no config, using defaults\n");
  }

  PushoverComfig(PVC);
  SystemloadConfiguration(SC);
  PrintWiFiConfigStruct(WFC);
  return 1;
}

/* ── System config ───────────────────────────────────────────────────────── */

/** @brief Load system config from LittleFS JSON; keeps defaults if file absent. */
void SystemloadConfiguration(struct SystemConfig* SC) {
  if (!LittleFS.exists(Systemfilename)) return;
  File file = LittleFS.open(Systemfilename);
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok)
    SC->rs485Mode = doc["rs485"] | false;
  file.close();
}

/** @brief Save system config to LittleFS JSON. */
void SystemsaveConfiguration(struct SystemConfig* SC) {
  LittleFS.remove(Systemfilename);
  File file = LittleFS.open(Systemfilename, "w");
  if (file) {
    StaticJsonDocument<64> doc;
    doc["rs485"] = SC->rs485Mode;
    serializeJson(doc, file);
    file.close();
  }
}

/* ── WiFi config JSON ────────────────────────────────────────────────────── */

/** @brief Load WiFi config from JSON file; reboots on parse error. */
void WifiloadConfiguration(struct WiFiConfig* WFC) {
  File file = LittleFS.open(WiFifilename);
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    LittleFS.remove(WiFifilename);
    delay(1000);
    ESP.restart();
  } else {
    strlcpy(WFC->SSID,          doc["SSID"],          sizeof(WFC->SSID));
    strlcpy(WFC->Passcode,      doc["Passcode"],      sizeof(WFC->Passcode));
    strlcpy(WFC->Host,          doc["Host"],          sizeof(WFC->Host));
    strlcpy(WFC->IP,            doc["IP"],            sizeof(WFC->IP));
    strlcpy(WFC->DefultGateway, doc["DefultGateway"], sizeof(WFC->DefultGateway));
    strlcpy(WFC->SubMask,       doc["SubMask"],       sizeof(WFC->SubMask));
    WFC->WIFIMode = doc["WIFIMode"];
    WFC->SSIDLN   = doc["SSIDLN"];
    WFC->PswdLN   = doc["PswdLN"];
    WFC->HoastLN  = doc["HoastLN"];
    WFC->DHCP     = doc["DHCP"];
    file.close();
  }
}

/** @brief Save WiFi config to LittleFS JSON and NVS. */
void WifisaveConfiguration(struct WiFiConfig* WFC) {
  LittleFS.remove(WiFifilename);
  DefaultsLoaded = 1;
  File file = LittleFS.open(WiFifilename, "w");
  if (file) {
    StaticJsonDocument<512> doc;
    doc["WIFIMode"]      = WFC->WIFIMode;
    doc["SSID"]          = WFC->SSID;
    doc["SSIDLN"]        = WFC->SSIDLN;
    doc["Passcode"]      = WFC->Passcode;
    doc["PswdLN"]        = WFC->PswdLN;
    doc["Host"]          = WFC->Host;
    doc["HoastLN"]       = WFC->HoastLN;
    doc["DHCP"]          = WFC->DHCP;
    doc["IP"]            = WFC->IP;
    doc["DefultGateway"] = WFC->DefultGateway;
    doc["SubMask"]       = WFC->SubMask;
    if (serializeJson(doc, file) == 0)
      Log(ERROR, "Failed to write WiFi config file\n");
    file.close();
  }
  NVSsaveWifi(WFC);
}

/** @brief Load or create the WiFi JSON config file. */
void WifiComfig(struct WiFiConfig* WFC) {
  if (LittleFS.exists(WiFifilename)) {
    WifiloadConfiguration(WFC);
    delay(100);
    PrintWiFiConfigStruct(WFC);
    delay(100);
  } else {
    WifisaveConfiguration(WFC);
    WifiloadConfiguration(WFC);
    delay(100);
    PrintWiFiConfigStruct(WFC);
    delay(100);
  }
}

/* ── MQTT config JSON ────────────────────────────────────────────────────── */

/** @brief Load MQTT config from JSON file; reboots on parse error. */
void MqttloadConfiguration(struct MQTTConfig* MQC) {
  File file = LittleFS.open(MQTTfilename);
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Log(ERROR, "MQTT config read error — resetting to defaults\n");
    LittleFS.remove(MQTTfilename);
    delay(1000);
    ESP.restart();
  } else {
    strlcpy(MQC->MQTTIP,       doc["MQTTIP"]       | MQC->MQTTIP,       sizeof(MQC->MQTTIP));
    strlcpy(MQC->MQTTUser,     doc["MQTTUser"]     | MQC->MQTTUser,     sizeof(MQC->MQTTUser));
    strlcpy(MQC->MQTTPassword, doc["MQTTPassword"] | MQC->MQTTPassword, sizeof(MQC->MQTTPassword));
    MQC->MQTTEnabble    = doc["MQTTEnabble"]    | MQC->MQTTEnabble;
    MQC->MQTTPort       = doc["MQTTPort"]       | MQC->MQTTPort;
    MQC->MQTTPasswordLN = doc["MQTTPasswordLN"] | MQC->MQTTPasswordLN;
    file.close();
  }
}

/** @brief Save MQTT config to LittleFS JSON and NVS. */
void MqttsaveConfiguration(struct MQTTConfig* MQC) {
  LittleFS.remove(MQTTfilename);
  File file = LittleFS.open(MQTTfilename, "w");
  if (file) {
    StaticJsonDocument<512> doc;
    doc["MQTTEnabble"]    = MQC->MQTTEnabble;
    doc["MQTTIP"]         = MQC->MQTTIP;
    doc["MQTTPort"]       = MQC->MQTTPort;
    doc["MQTTUser"]       = MQC->MQTTUser;
    doc["MQTTPassword"]   = MQC->MQTTPassword;
    doc["MQTTPasswordLN"] = MQC->MQTTPasswordLN;
    if (serializeJson(doc, file) == 0)
      Log(ERROR, "Failed to write MQTT config file\n");
    file.close();
  }
  NVSsaveMQTT(MQC);
}

/** @brief Load or create the MQTT JSON config file. */
void MqttComfig(struct MQTTConfig* MQC) {
  if (LittleFS.exists(MQTTfilename)) {
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  } else {
    MqttsaveConfiguration(MQC);
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  }
}

/* ── Pushover config JSON ────────────────────────────────────────────────── */

/** @brief Load or create the Pushover JSON config file. */
void PushoverComfig(struct PushoverConfig* PVC) {
  if (LittleFS.exists(Pushoverfilename)) {
    PushoverloadConfiguration(PVC);
  } else {
    PushoversaveConfiguration(PVC);
    PushoverloadConfiguration(PVC);
  }
}

/** @brief Load Pushover credentials from LittleFS JSON. */
void PushoverloadConfiguration(struct PushoverConfig* PVC) {
  File file = LittleFS.open(Pushoverfilename);
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok) {
    PVC->enabled = doc["enabled"] | PVC->enabled;
    strlcpy(PVC->token,   doc["token"]   | PVC->token,   sizeof(PVC->token));
    strlcpy(PVC->userKey, doc["userKey"] | PVC->userKey, sizeof(PVC->userKey));
  }
  file.close();
}

/** @brief Save Pushover credentials to LittleFS JSON. */
void PushoversaveConfiguration(struct PushoverConfig* PVC) {
  LittleFS.remove(Pushoverfilename);
  File file = LittleFS.open(Pushoverfilename, "w");
  if (file) {
    StaticJsonDocument<256> doc;
    doc["enabled"] = PVC->enabled;
    doc["token"]   = PVC->token;
    doc["userKey"] = PVC->userKey;
    if (serializeJson(doc, file) == 0)
      Log(ERROR, "Failed to write Pushover config file\n");
    file.close();
  }
}

/* ── Cellular config JSON ────────────────────────────────────────────────── */

/** @brief Load or create the Cellular JSON config file. */
void CellularComfig(struct CellularConfig* CC) {
  if (LittleFS.exists(Cellularfilename)) {
    CellularloadConfiguration(CC);
  } else {
    CellularsaveConfiguration(CC);
  }
}

/** @brief Load cellular duty-cycle config from LittleFS JSON. */
void CellularloadConfiguration(struct CellularConfig* CC) {
  File file = LittleFS.open(Cellularfilename);
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok)
    CC->heartbeatMins = doc["heartbeatMins"] | 10;
  file.close();
}

/** @brief Save cellular duty-cycle config to LittleFS JSON. */
void CellularsaveConfiguration(struct CellularConfig* CC) {
  LittleFS.remove(Cellularfilename);
  File file = LittleFS.open(Cellularfilename, "w");
  if (file) {
    StaticJsonDocument<64> doc;
    doc["heartbeatMins"] = CC->heartbeatMins;
    if (serializeJson(doc, file) == 0)
      Log(ERROR, "Failed to write Cellular config file\n");
    file.close();
  }
}

/* ── Debug print helpers ─────────────────────────────────────────────────── */

/** @brief Log WiFi config summary (SSID, hostname, DHCP flag) at LOG level. */
void PrintWiFiConfigStruct(struct WiFiConfig* WFC) {
  Log(LOG, "WiFi SSID: %s  Host: %s  DHCP: %d\n", WFC->SSID, WFC->Host, WFC->DHCP);
}

/** @brief Log MQTT config summary (IP, port, user) at LOG level. */
void PrintMqttConfigStruct(struct MQTTConfig* MQC) {
  Log(LOG, "MQTT IP: %s  Port: %d  User: %s\n", MQC->MQTTIP, MQC->MQTTPort, MQC->MQTTUser);
}

/* ── LittleFS directory listing ──────────────────────────────────────────── */

/**
 * @brief Recursively log LittleFS directory contents at DEBUG level.
 * @param fs      File-system reference.
 * @param dirname Directory path to list.
 * @param levels  Remaining recursion depth (0 = no recursion).
 */
static void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Log(DEBUG, "  DIR: %s\n", file.name());
            if (levels) listDir(fs, file.name(), levels - 1);
        } else {
            Log(DEBUG, "  FILE: %s  SIZE: %d\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

/** @brief Write a string to a LittleFS file (utility, currently unused). */
static void writeFile(fs::FS& fs, const char* path, const char* message) {
    File file = fs.open(path, "w");
    if (file) { file.print(message); file.close(); }
}
