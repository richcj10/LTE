/**
 * @file MQTT.h
 * @brief MQTT client with Home Assistant MQTT Discovery auto-configuration.
 *
 * Connects to the broker configured in LittleFS/NVS.  On first connection it
 * publishes retained discovery payloads for 14 Home Assistant entities covering
 * battery, cellular, and GPS telemetry.
 *
 * Subscribes to @c Cellular/CellMsg for inbound Pushover commands.
 * All state is published to @c Cellular/ topics by MQTTMessageUpdate().
 */
#ifndef MQTT_H
#define MQTT_H

#define PO 22  /**< Legacy: Pushover destination identifier (unused in current code) */

/**
 * @brief Configure the MQTT client — server address, port, credentials, and device ID.
 *  Call once during startup after WiFi connects.
 */
void MQTTStart(void);

/**
 * @brief Service the MQTT client connection.
 *
 * When connected calls client.loop(); when disconnected attempts MQTTreconnect().
 * A 2-minute lockout prevents rapid-fire reconnect storms.
 *
 * Call from the main loop every iteration.
 */
void MqttLoop(void);

/**
 * @brief Attempt to connect to the MQTT broker (up to 3 tries).
 *
 * On success, subscribes to @c Cellular/CellMsg and @c Cellular/CellCMD,
 * publishes Home Assistant discovery payloads, and performs an initial
 * MQTTMessageUpdate().
 */
void MQTTreconnect(void);

/**
 * @brief Publish all current sensor values to their MQTT topics.
 *
 * Topics published (all under the @c Cellular/ prefix):
 * CellVoltage, CellPWR, CellRate, PowerMode, CellAlert, CellStatus,
 * CellSignal, CellConnected, GPSFix, GPS, GPSLat, GPSLon, GPSSpeed,
 * GPSAlt, GPSHeading.
 */
void MQTTMessageUpdate(void);

/** @return @c 1 when the broker connection is active, @c 0 otherwise. */
char GetMQTTStatus(void);

/** @return Current inbound message type code. */
char GetMQTTMsg();

/** @brief Clear the inbound message type flag. */
void CLRMQTTMsg();

/* Legacy stubs — not connected to hardware in this build */
void SendChestFreezer(void);
void SendDeviceEnviroment(void);
void SendChestPower(char Mode);

#endif /* MQTT_H */
