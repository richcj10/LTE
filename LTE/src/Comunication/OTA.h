/**
 * @file OTA.h
 * @brief ArduinoOTA WiFi over-the-air firmware update support.
 *
 * Firmware updates are WiFi-only; there is no bootloader-over-RS-485 path.
 * The OTA hostname matches the device unique name (e.g. @c ESPPLC-LTE-A1B2)
 * so it is discoverable from PlatformIO or the Arduino IDE.
 */
#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

/**
 * @brief Register ArduinoOTA callbacks and start the OTA service.
 *  Call once during startup after WiFi is connected.
 */
void OTAsetup();

/**
 * @brief Handle pending OTA events.
 *  Call from the main loop every iteration.
 */
void OTAloop();

/** @return @c true while a firmware upload is in progress. */
bool OTAactive();

#endif /* OTA_H */
