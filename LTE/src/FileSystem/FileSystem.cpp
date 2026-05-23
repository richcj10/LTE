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

const char *WiFifilename      = "/WiFiconfig.json";
const char *MQTTfilename      = "/MQTTconfig.json";
const char *Systemfilename    = "/system.json";
const char *Pushoverfilename  = "/Pushoverconfig.json";
const char *Cellularfilename  = "/Cellularconfig.json";
const char *MQTTTopicsfilename = "/MQTTcTopics.json";
const char *Remotefilename    = "/Remote.json";

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void writeFile(fs::FS &fs, const char * path, const char * message);

bool DefaultsLoaded = 0;

// ── NVS load / save ───────────────────────────────────────────
bool NVSloadWifi(struct WiFiConfig* WFC) {
  Preferences p;
  p.begin("wifi", true);
  String ssid = p.getString("ssid", "");
  p.end();
  if (ssid.length() == 0) return false;
  p.begin("wifi", true);
  strlcpy(WFC->SSID,    ssid.c_str(),                      sizeof(WFC->SSID));
  WFC->SSIDLN = ssid.length();
  String pass = p.getString("pass", "");
  strlcpy(WFC->Passcode, pass.c_str(),                     sizeof(WFC->Passcode));
  WFC->PswdLN = pass.length();
  String host = p.getString("host", WFC->Host);
  strlcpy(WFC->Host,    host.c_str(),                      sizeof(WFC->Host));
  WFC->HoastLN = host.length();
  WFC->DHCP     = p.getUChar("dhcp", WFC->DHCP);
  WFC->WIFIMode = p.getUChar("mode", WFC->WIFIMode);
  p.end();
  return true;
}

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

bool NVSloadMQTT(struct MQTTConfig* MQC) {
  Preferences p;
  p.begin("mqtt", true);
  String ip = p.getString("ip", "");
  p.end();
  if (ip.length() == 0) return false;
  p.begin("mqtt", true);
  strlcpy(MQC->MQTTIP,       ip.c_str(),                    sizeof(MQC->MQTTIP));
  MQC->MQTTPort     = p.getUInt("port",  MQC->MQTTPort);
  MQC->MQTTEnabble  = p.getUChar("en",   MQC->MQTTEnabble);
  String user = p.getString("user", "");
  strlcpy(MQC->MQTTUser,     user.c_str(),                  sizeof(MQC->MQTTUser));
  String pass = p.getString("pass", "");
  strlcpy(MQC->MQTTPassword, pass.c_str(),                  sizeof(MQC->MQTTPassword));
  MQC->MQTTPasswordLN = pass.length();
  p.end();
  return true;
}

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

char FileSystemInit(struct WiFiConfig* WFC, struct MQTTConfig* MQC, struct SystemConfig* SC, struct PushoverConfig* PVC){
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Log(ERROR, "LittleFS Mount Failed\n");
    return 0;
  }
  listDir(LittleFS, "/", 0);

  /* ── WiFi: NVS → JSON file → AP mode ───────────────────────────
     Priority: NVS first. If empty, import from JSON file and save to
     NVS so future boots use NVS directly. If neither exists, signal
     that AP mode is needed (return 2). */
  if (NVSloadWifi(WFC)) {
    Log(NOTIFY, "WiFi: config from NVS (ssid=%s)\n", WFC->SSID);
  } else if (LittleFS.exists(WiFifilename)) {
    Log(NOTIFY, "WiFi: importing JSON → NVS\n");
    WifiloadConfiguration(WFC);
    NVSsaveWifi(WFC);
  } else {
    Log(NOTIFY, "WiFi: no config found — AP mode needed\n");
    /* Still load MQTT/Pushover/system so they're available after AP setup */
    PushoverComfig(PVC);
    SystemloadConfiguration(SC);
    return 2;   /* caller must start AP mode */
  }

  /* ── MQTT: NVS → JSON file → defaults (not fatal) ─────────────── */
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

void SystemloadConfiguration(struct SystemConfig* SC) {
  if (!LittleFS.exists(Systemfilename)) return;  /* keep defaults if no file */
  File file = LittleFS.open(Systemfilename);
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok)
    SC->rs485Mode = doc["rs485"] | false;
  file.close();
}

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

void WifiComfig(struct WiFiConfig* WFC){
  if(LittleFS.exists(WiFifilename)){
    //LOG("Found File.....Load Config!\r");
    WifiloadConfiguration(WFC);
    delay(100);
    PrintWiFiConfigStruct(WFC);
    delay(100);
  }
  else{
    //LOG("No Wifi Config! Save One!....");
    WifisaveConfiguration(WFC);
    //OG("Now Load Config! \r");            
    WifiloadConfiguration(WFC);
    delay(100);
    PrintWiFiConfigStruct(WFC);
    delay(100);
  }
}

void MqttComfig(struct MQTTConfig* MQC){
  if(LittleFS.exists(MQTTfilename)){
    //LOG("Found File.....Load Config!\r");
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  }
  else{
    //Config Doc dson't exist, wite one!
    //LOG("No MQTT Config! Save One!....");
    MqttsaveConfiguration(MQC);
    //LOG("Now Load Config! \r");            
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  }
}

void RemoteComfig(){
/*   if(LittleFS.exists(MQTTfilename)){
    LOG("Found File.....Load Config!\r");
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  }
  else{
    //Config Doc dson't exist, wite one!
    LOG("No MQTT Config! Save One!....");
    MqttsaveConfiguration(MQC);
    LOG("Now Load Config! \r");            
    MqttloadConfiguration(MQC);
    delay(100);
    PrintMqttConfigStruct(MQC);
    delay(100);
  } */
}

// Loads the configuration from a file
void WifiloadConfiguration(struct WiFiConfig* WFC) {
  // Open file for reading
  File file = LittleFS.open(WiFifilename);

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error){
    //LOG("WiFi - File Read Error, Rebuilding file from defults ****Rebooting****\r");
    LittleFS.remove(WiFifilename);
    delay(1000);
    ESP.restart(); //Reboot the device and load defaults. 
  }
  else{
    // Copy values from the JsonDocument to the Config
    strlcpy(WFC->SSID,doc["SSID"],sizeof(WFC->SSID));
    strlcpy(WFC->Passcode,doc["Passcode"],sizeof(WFC->Passcode));
    strlcpy(WFC->Host,doc["Host"],sizeof(WFC->Host));
    strlcpy(WFC->IP,doc["IP"],sizeof(WFC->IP));
    strlcpy(WFC->DefultGateway,doc["DefultGateway"],sizeof(WFC->DefultGateway));
    strlcpy(WFC->SubMask,doc["SubMask"],sizeof(WFC->SubMask));

    WFC->WIFIMode = doc["WIFIMode"];
    WFC->SSIDLN = doc["SSIDLN"];
    WFC->PswdLN = doc["PswdLN"];
    WFC->HoastLN = doc["HoastLN"];
    WFC->DHCP = doc["DHCP"];
    // Close the file (Curiously, File's destructor doesn't close the file)
    //SPIFFS.close();
    file.close();
  }
}

// Saves the configuration to LittleFS AND NVS
void WifisaveConfiguration(struct WiFiConfig* WFC) {
  // Delete old file for updating.
  LittleFS.remove(WiFifilename);
  DefaultsLoaded = 1;
  // Open file for writing
  File file = LittleFS.open(WiFifilename, "w");
  if (file) {
    //LOG("Opened File! \r");

    StaticJsonDocument<512> doc; // Allocate a temporary JsonDocument

    /*
    * This is to save the autogenerated host name into data dictonary
    * This is ran on inital boot or file system couruption. 
    */

    //strlcpy(WFC->Host,GetClientId().c_str(),strlen(GetClientId().c_str())); // copy generated string into array
    //WFC->HoastLN = strlen(GetClientId().c_str()); // Save lenth

    // Set the values in the document
    doc["WIFIMode"] = WFC->WIFIMode;
    doc["SSID"] = WFC->SSID;
    doc["SSIDLN"] =  WFC->SSIDLN;
    doc["Passcode"] =  WFC->Passcode;
    doc["PswdLN"] =  WFC->PswdLN;
    doc["Host"] =  WFC->Host;
    doc["HoastLN"] =  WFC->HoastLN;
    doc["DHCP"] =  WFC->DHCP;
    doc["IP"] =  WFC->IP;
    doc["DefultGateway"] =  WFC->DefultGateway;
    doc["SubMask"] =  WFC->SubMask;
    // Serialize JSON to file
    //serializeJsonPretty(doc, Serial);
    if (serializeJson(doc, file) == 0)
      Log(ERROR, "Failed to write WiFi config file\n");
    file.close();
  }
  NVSsaveWifi(WFC);
}

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

void PushoverComfig(struct PushoverConfig* PVC) {
  if (LittleFS.exists(Pushoverfilename)) {
    PushoverloadConfiguration(PVC);
  } else {
    PushoversaveConfiguration(PVC);
    PushoverloadConfiguration(PVC);
  }
}

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

void CellularComfig(struct CellularConfig* CC) {
  if (LittleFS.exists(Cellularfilename)) {
    CellularloadConfiguration(CC);
  } else {
    CellularsaveConfiguration(CC);   /* write defaults on first boot */
  }
}

void CellularloadConfiguration(struct CellularConfig* CC) {
  File file = LittleFS.open(Cellularfilename);
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, file) == DeserializationError::Ok)
    CC->heartbeatMins = doc["heartbeatMins"] | 360;
  file.close();
}

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

void PrintWiFiConfigStruct(struct WiFiConfig* WFC){
  Log(LOG, "WiFi SSID: %s  Host: %s  DHCP: %d\n", WFC->SSID, WFC->Host, WFC->DHCP);
}

void PrintMqttConfigStruct(struct MQTTConfig* MQC){
  Log(LOG, "MQTT IP: %s  Port: %d  User: %s\n", MQC->MQTTIP, MQC->MQTTPort, MQC->MQTTUser);
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    File root = fs.open(dirname);
    if(!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Log(DEBUG, "  DIR: %s\n", file.name());
            if(levels) listDir(fs, file.name(), levels - 1);
        } else {
            Log(DEBUG, "  FILE: %s  SIZE: %d\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}