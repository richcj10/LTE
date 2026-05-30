/**
 * @file FSInterface.h
 * @brief Typed config accessors — thin facade over FileSystem.cpp.
 *
 * Initialises LittleFS on first call and loads all configuration structs.
 * NVS (ESP32 Non-Volatile Storage) is the primary store for WiFi and MQTT
 * credentials.  LittleFS JSON files are the import/fallback source and the
 * primary store for Pushover, Cellular, and System config.
 *
 * Client code should always go through these functions rather than accessing
 * the FileSystem structs directly.
 */
#ifndef FSINTERFACE_H
#define FSINTERFACE_H
#include "Arduino.h"

/**
 * @brief Mount LittleFS, load all config structs, and determine if AP mode is needed.
 * @return @c 1 = OK (WiFi creds loaded), @c 2 = AP mode needed (no WiFi creds), @c 0 = FS error.
 */
char FileStstemStart();

/** @return @c true when no WiFi credentials were found and AP mode is required. */
bool NeedsAPMode();

/** @defgroup fsi_wifi WiFi credential accessors
 *  @{
 */
/** @return WiFi mode byte (2 = STA). */
unsigned char GetWiFiMode();
/** @return Stored network SSID. */
String GetSSID();
/** @return Stored WiFi passphrase. */
String GetSSIDPassword();
/** @return Device mDNS hostname. */
String GetHostName();
/** @} */

/** @defgroup fsi_mqtt MQTT broker accessors
 *  @{
 */
/** @return @c 1 if MQTT publishing is enabled, @c 0 if disabled. */
unsigned char GetMQTTEnabled();
/** @return MQTT broker IP address string. */
String        GetMQTTIP();
/** @return MQTT broker TCP port. */
unsigned int  GetMQTTPort();
/** @return MQTT username. */
String        GetMQTTUser();
/** @return MQTT password. */
String        GetMQTTPassword();
/** @} */

/**
 * @return @c true when RS-485/Modbus mode is active and UART0 is owned by the Modbus slave.
 */
bool GetRS485Mode();

/**
 * @brief Enable or disable RS-485 mode and persist the setting to LittleFS.
 * @param on @c true to give UART0 to Modbus; @c false to restore serial debug output.
 */
void SetRS485Mode(bool on);

/** @defgroup fsi_pushover Pushover credential accessors
 *  @{
 */
/** @return @c 1 if Pushover notifications are enabled, @c 0 otherwise. */
unsigned char GetPushoverEnabled();
/** @return Pushover application API token. */
String        GetPushoverToken();
/** @return Pushover user/group key. */
String        GetPushoverUserKey();
/**
 * @brief Update Pushover credentials and persist them to LittleFS.
 * @param enabled @c 1 to enable notifications, @c 0 to disable.
 * @param token   Application token (max 47 chars).
 * @param userKey User or group key (max 47 chars).
 */
void          SetPushoverConfig(unsigned char enabled, const char* token, const char* userKey);
/** @} */

/** @defgroup fsi_cellular Cellular duty-cycle config
 *  @{
 */
/**
 * @return Heartbeat interval in minutes — the modem sleep duration between
 *         duty-cycle wakes when no pending jobs exist.
 */
unsigned int GetHeartbeatMins();
/**
 * @brief Update the heartbeat interval and persist it to LittleFS.
 * @param mins New interval in minutes (minimum 1).
 */
void         SetHeartbeatMins(unsigned int mins);
/** @} */

/**
 * @brief Save new WiFi credentials to NVS and clear the AP-mode flag.
 * @param ssid New network SSID.
 * @param pass New WPA2 passphrase.
 * @param host New mDNS hostname.
 */
void SaveWifiToNVS(const char* ssid, const char* pass, const char* host);

/**
 * @brief Save new MQTT broker settings to NVS.
 * @param ip      Broker IP address string.
 * @param port    Broker TCP port.
 * @param user    MQTT username.
 * @param pass    MQTT password.
 * @param enabled @c 1 to enable MQTT, @c 0 to disable.
 */
void SaveMQTTToNVS(const char* ip, unsigned int port, const char* user, const char* pass, unsigned char enabled);

#endif /* FSINTERFACE_H */
