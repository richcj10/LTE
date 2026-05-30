/**
 * @file FileSystem.h
 * @brief Low-level LittleFS / NVS storage layer and all configuration struct definitions.
 *
 * Config structs are defined here so both FileSystem.cpp and FSInterface.cpp
 * can use them without circular dependencies.  Consumer code should call the
 * FSInterface.h accessor functions rather than these low-level routines directly.
 *
 * Config files on LittleFS:
 *   /WiFiconfig.json, /MQTTconfig.json, /system.json,
 *   /Pushoverconfig.json, /Cellularconfig.json
 */
#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <Preferences.h>

/** @brief System-level configuration flags. */
struct SystemConfig {
  bool rs485Mode = false;  /**< @c true = RS-485/Modbus owns UART0; @c false = serial debug mode */
};

/** @brief WiFi network connection configuration. */
struct WiFiConfig {
  unsigned char WIFIMode = 2;                      /**< WiFi mode byte (2 = STA) */
  char SSID[32]          = "**REDACTED-SSID**"; /**< Target SSID */
  unsigned char SSIDLN   = 20;                     /**< SSID length in bytes */
  char Passcode[40]      = "**REDACTED-PASS**";             /**< WPA2 passphrase */
  unsigned char PswdLN   = 8;                      /**< Passphrase length */
  char Host[40]          = "ESP32PLC-xxxx";         /**< mDNS / ArduinoOTA hostname */
  unsigned char HoastLN  = 13;                     /**< Hostname length */
  unsigned char DHCP     = 1;                      /**< 1 = DHCP, 0 = static IP */
  char IP[16]            = "192.168.X.X";         /**< Static IP (DHCP = 0 only) */
  char DefultGateway[16] = "000.000.000.000";      /**< Static default gateway */
  char SubMask[16]       = "000.000.000.000";      /**< Static subnet mask */
};

/** @brief MQTT broker connection configuration. */
struct MQTTConfig {
  unsigned char MQTTEnabble  = 1;               /**< 1 = MQTT enabled */
  char MQTTIP[16]            = "192.168.X.X";/**< Broker IPv4 address */
  unsigned int  MQTTPort     = 1883;            /**< Broker TCP port */
  char MQTTUser[32]          = "ESPPLCCell";    /**< Authentication username */
  char MQTTPassword[40]      = "**REDACTED-MQTT-PASS**";    /**< Authentication password */
  unsigned char MQTTPasswordLN = 10;            /**< Password length in bytes */
};

/** @brief Pushover push-notification API credentials. */
struct PushoverConfig {
  unsigned char enabled = 0;  /**< 1 = Pushover notifications are enabled */
  char token[48]   = "";      /**< Pushover application API token */
  char userKey[48] = "";      /**< Pushover user or group key */
};

/** @brief Cellular modem duty-cycle configuration. */
struct CellularConfig {
  unsigned int heartbeatMins = 360;  /**< Modem sleep duration in minutes between duty-cycle wakes (default 6 h) */
};

/**
 * @brief Mount LittleFS and load all configuration structs.
 *
 * WiFi priority: NVS → JSON file → AP mode (return 2).
 * MQTT priority: NVS → JSON file → built-in defaults.
 * Pushover and System are always loaded from JSON (with built-in defaults on first boot).
 *
 * @param WFC Output WiFi config struct.
 * @param MQC Output MQTT config struct.
 * @param SC  Output system config struct.
 * @param PVC Output Pushover config struct.
 * @return @c 1 = OK, @c 2 = no WiFi creds (AP mode needed), @c 0 = LittleFS mount failed.
 */
char FileSystemInit(struct WiFiConfig* WFC, struct MQTTConfig* MQC, struct SystemConfig* SC, struct PushoverConfig* PVC);

/**
 * @brief Load WiFi credentials from NVS.
 * @return @c true if valid credentials were found.
 */
bool NVSloadWifi(struct WiFiConfig* WFC);

/** @brief Save WiFi credentials to NVS. */
void NVSsaveWifi(struct WiFiConfig* WFC);

/**
 * @brief Load MQTT config from NVS.
 * @return @c true if a valid entry was found.
 */
bool NVSloadMQTT(struct MQTTConfig* MQC);

/** @brief Save MQTT config to NVS. */
void NVSsaveMQTT(struct MQTTConfig* MQC);

/** @brief Persist the system config to LittleFS JSON. */
void SystemsaveConfiguration(struct SystemConfig* SC);
/** @brief Load the system config from LittleFS JSON (keeps defaults on missing file). */
void SystemloadConfiguration(struct SystemConfig* SC);

/** @brief Save WiFi config to LittleFS JSON and NVS. */
void WifisaveConfiguration(struct WiFiConfig* WFC);
/** @brief Load WiFi config from LittleFS JSON; reboots on parse error. */
void WifiloadConfiguration(struct WiFiConfig* WFC);

/** @brief Save MQTT config to LittleFS JSON and NVS. */
void MqttsaveConfiguration(struct MQTTConfig* MQC);
/** @brief Load MQTT config from LittleFS JSON; reboots on parse error. */
void MqttloadConfiguration(struct MQTTConfig* MQC);

/** @brief Save Pushover config to LittleFS JSON. */
void PushoversaveConfiguration(struct PushoverConfig* PVC);
/** @brief Load Pushover config from LittleFS JSON. */
void PushoverloadConfiguration(struct PushoverConfig* PVC);

/** @brief Print WiFi config struct to the log. */
void PrintWiFiConfigStruct(struct WiFiConfig* WFC);
/** @brief Print MQTT config struct to the log. */
void PrintMqttConfigStruct(struct MQTTConfig* MQC);

/** @brief Load or create the WiFi config JSON file. */
void WifiComfig(struct WiFiConfig* WFC);
/** @brief Load or create the MQTT config JSON file. */
void MqttComfig(struct MQTTConfig* MQC);
/** @brief Load or create the Pushover config JSON file. */
void PushoverComfig(struct PushoverConfig* PVC);
/** @brief Load or create the Cellular config JSON file. */
void CellularComfig(struct CellularConfig* CC);
/** @brief Load Cellular config from LittleFS JSON. */
void CellularloadConfiguration(struct CellularConfig* CC);
/** @brief Save Cellular config to LittleFS JSON. */
void CellularsaveConfiguration(struct CellularConfig* CC);

/** @brief Save the mDNS hostname into the WiFi config struct (not persisted here). */
void SaveHostName(struct WiFiConfig* WFC);

#endif /* FILESYSTEM_H */
