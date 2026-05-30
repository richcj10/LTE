/**
 * @file cellular.h
 * @brief Public API for the SIM7070G LTE cellular stack.
 *
 * The cellular stack runs entirely on Core 0 in its own FreeRTOS task.
 * Jobs (Pushover, SMS) are submitted via a lock-free queue.  State
 * accessors use a mutex or volatile reads and are safe to call from any
 * core or task.
 */
#ifndef CELLULAR_H
#define CELLULAR_H

#include <Arduino.h>

/** @defgroup cell_init Task lifecycle
 *  @{
 */
/** @brief Initialise resources and pin the cellular FreeRTOS task to Core 0.
 *  Call once from setup(). */
void CellTaskStart();
/** @} */

/** @defgroup cell_notify Notifications
 *  @{
 */
/**
 * @brief Queue a Pushover HTTPS push notification (non-blocking).
 *
 * Returns immediately.  The HTTP POST is performed by the cellular task.
 * If the modem is sleeping the task is woken automatically.
 *
 * @param title   Notification title — max 47 chars.
 * @param message Notification body  — max 127 chars.
 * @return @c true if the job was enqueued; @c false if the queue is full (depth 8).
 */
bool Pushover(const char* title, const char* message);

/**
 * @brief Queue an SMS message (non-blocking).
 * @param number  Destination E.164 number, e.g. @c "+12125551234".
 * @param message Message body; keep to ≤160 chars for a single-part SMS.
 * @return @c true if enqueued; @c false if the queue is full.
 */
bool SendTextMsg(const char* number, const char* message);
/** @} */

/** @defgroup gps GPS control and position data
 *  @{
 */
/**
 * @brief Enable or disable the SIM7070G built-in GNSS receiver.
 * @param on @c true to power the receiver on; @c false to power it off.
 */
void   GPSenable(bool on);
/** @return @c true while the GNSS receiver is powered. */
bool   GPSisEnabled();
/** @return @c true when a valid position fix has been obtained. */
bool   GPShasFix();
/** @return Latitude in decimal degrees.  Valid only when GPShasFix() returns @c true. */
float  GPSlat();
/** @return Longitude in decimal degrees.  Valid only when GPShasFix() returns @c true. */
float  GPSlon();
/** @return Speed over ground in km/h.  Valid only when GPShasFix() returns @c true. */
float  GPSspeed();
/** @return Course over ground in degrees (0–360).  Valid only when GPShasFix() is @c true. */
float  GPSheading();
/** @return MSL altitude in metres.  Valid only when GPShasFix() returns @c true. */
float  GPSaltitude();
/** @return Latitude as a decimal string, or @c "N/A" when there is no fix. */
String GPSlatString();
/** @return Longitude as a decimal string, or @c "N/A" when there is no fix. */
String GPSlonString();
/** @} */

/** @defgroup cell_state LTE modem state accessors (all thread-safe)
 *  @{
 */
/** @return @c true while the modem is powered on. */
bool   CellIsOn();
/** @return @c true while the LTE data bearer (PDP context 0) is active. */
bool   CellIsConnected();
/** @return Most-recent RSSI in dBm; @c 0 = unknown. */
int8_t CellSig();
/** @return Human-readable state-machine label, e.g. @c "Online". */
String CellStateStr();
/** @return Short operational status string, e.g. @c "Connected" or @c "Error". */
String CellStatString();
/** @return 20-digit SIM ICCID string, or an em-dash if not yet retrieved. */
String CellSIMString();
/** @return RSSI value as a decimal string. */
String CellSigString();
/** @return APN network name (always @c "hologram"). */
String CellNetworkString();
/** @return Bearer IP address (always @c "N/A" — not tracked). */
String CellIPString();
/** @return GPS state as @c "OFF", @c "No Fix", or @c "Fix". */
String CellGPSString();
/** @return Latitude string from the most recent GNSS poll. */
String CellLATString();
/** @return Longitude string from the most recent GNSS poll. */
String CellLONString();
/** @brief Log LTE modem status to the system log at LOG level. */
void   CellularDisplay();

/**
 * @brief Retrieve the title and outcome of the most recent Pushover attempt.
 * @param[out] titleOut  Caller buffer (≥48 bytes) for the notification title.
 * @param[out] okOut     Set to @c true if the last attempt returned HTTP 200.
 * @param[out] statusOut Caller buffer (≥40 bytes) for the status string.
 */
void     CellLastPushover(char* titleOut, bool* okOut, char* statusOut);

/**
 * @brief Seconds until the next scheduled modem wake in duty-cycle sleep mode.
 * @return Seconds remaining, or @c 0 if the modem is currently active.
 */
uint32_t CellNextHeartbeatSecs();
/** @} */

#endif /* CELLULAR_H */
