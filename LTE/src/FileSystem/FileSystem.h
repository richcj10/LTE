#ifndef FILESYSTEM_H
#define  FILESYSTEM_H

#include <Preferences.h>

struct SystemConfig {
  bool rs485Mode = false;   /* true = RS485/Modbus on UART0, false = serial debug */
};

char FileSystemInit(struct WiFiConfig* WFC, struct MQTTConfig* MQC, struct SystemConfig* SC, struct PushoverConfig* PVC);

/* NVS (Non-Volatile Storage) — primary config store for WiFi and MQTT */
bool NVSloadWifi(struct WiFiConfig* WFC);   /* true if NVS had valid creds */
void NVSsaveWifi(struct WiFiConfig* WFC);
bool NVSloadMQTT(struct MQTTConfig* MQC);   /* true if NVS had valid entry  */
void NVSsaveMQTT(struct MQTTConfig* MQC);

void SystemsaveConfiguration(struct SystemConfig* SC);
void SystemloadConfiguration(struct SystemConfig* SC);
void WifisaveConfiguration(struct WiFiConfig* WFC);
void WifiloadConfiguration(struct WiFiConfig* WFC);
void MqttsaveConfiguration(struct MQTTConfig* MQC);
void MqttloadConfiguration(struct MQTTConfig* MQC);
void PushoversaveConfiguration(struct PushoverConfig* PVC);
void PushoverloadConfiguration(struct PushoverConfig* PVC);
void PrintWiFiConfigStruct(struct WiFiConfig* WFC);
void PrintMqttConfigStruct(struct MQTTConfig* MQC);
void WifiComfig(struct WiFiConfig* WFC);
void MqttComfig(struct MQTTConfig* MQC);
void PushoverComfig(struct PushoverConfig* PVC);
void CellularComfig(struct CellularConfig* CC);
void CellularloadConfiguration(struct CellularConfig* CC);
void CellularsaveConfiguration(struct CellularConfig* CC);
void SaveHostName(struct WiFiConfig* WFC);

struct WiFiConfig {
  unsigned char WIFIMode = 2;
  char SSID[32] = "**REDACTED-SSID**";
  unsigned char SSIDLN = 20;
  char Passcode[40] = "**REDACTED-PASS**";
  unsigned char PswdLN = 8;
  char Host[40] ="ESP32PLC-xxxx";
  unsigned char HoastLN = 13;
  unsigned char DHCP = 1;
  char IP[16] = "192.168.X.X";
  char DefultGateway[16] = "000.000.000.000";
  char SubMask[16] = "000.000.000.000";
};

struct MQTTConfig {
  unsigned char MQTTEnabble = 1;
  char MQTTIP[16]       = "192.168.X.X";
  unsigned int  MQTTPort = 1883;
  char MQTTUser[32]     = "ESPPLCCell";
  char MQTTPassword[40] = "**REDACTED-MQTT-PASS**";
  unsigned char MQTTPasswordLN = 10;
};

struct PushoverConfig {
  unsigned char enabled = 0;
  char token[48]   = "";
  char userKey[48] = "";
};

struct CellularConfig {
  unsigned int heartbeatMins = 360;   /* duty-cycle sleep interval, default 6 h */
};



#endif