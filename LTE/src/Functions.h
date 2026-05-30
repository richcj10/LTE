/**
 * @file Functions.h
 * @brief Top-level system startup and main-loop orchestration.
 */
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>

/**
 * @brief Full system initialisation sequence.
 *
 * Initialises (in order): GPIO, NeoPixel, unique device name, file system,
 * MAX17048 fuel gauge, RS-485 or serial debug, WiFi + web portal + OTA,
 * MQTT, and the cellular FreeRTOS task.
 *
 * @param WifiMode 1 = start WiFi, web portal, OTA, and MQTT; 0 = skip all.
 * @param LTEMode  1 = start the cellular task on Core 0; 0 = skip.
 * @return Always @c 1.
 */
char Startup(bool WifiMode, bool LTEMode);

/**
 * @brief Per-iteration main-loop dispatcher.
 *
 * Drives OTA, web portal, NeoPixel, WiFi AP, MQTT, RS-485 Modbus, fuel gauge,
 * WiFi watchdog, and power-loss/restore Pushover notifications — each at its
 * configured interval.
 *
 * Call from loop() every iteration without delay.
 */
void RunLoop();

/**
 * @brief Periodic debug print dispatcher.
 *
 * Fires FGDisplay() and PrintTime() every DEBUG_LOOP ms.
 * Call from loop() every iteration.
 */
void DebugPrint();

/**
 * @brief Derive and store the unique device name from the MAC address.
 *
 * Name format: @c "ESPPLC-LTE-XXXXX" where XXXXX is the last 5 nibbles
 * of the Wi-Fi MAC address.
 */
void UniqueName();

/** @brief Legacy LTE loop stub — no-op (cellular runs in its own FreeRTOS task). */
void LTELoop();

/**
 * @return Device unique name string, e.g. @c "ESPPLC-LTE-A1B2C".
 *  Empty string until UniqueName() has been called.
 */
String GetUniqueName();

#endif /* FUNCTIONS_H */
