/**
 * @file Wifi.h
 * @brief WiFi connection manager — STA mode with captive-portal AP fallback.
 *
 * On boot the firmware tries to connect to the SSID stored in NVS for up to
 * 30 s.  If that fails (or no credentials exist) it starts a WPA2 access point
 * named @c LTE-Setup-XXXX (password @c LTEAP) with a captive-portal DNS server
 * that redirects all DNS to the device IP so a browser opens the web portal
 * immediately.  After 5 minutes idle in AP mode (with no client connected) the
 * device restarts to retry STA.
 */
#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

/**
 * @brief Attempt STA connection; start captive-portal AP on failure.
 *
 * Reads credentials from NVS via FSInterface.  Blocks for up to 30 s while
 * connecting.
 *
 * @return @c 1 = STA connected, @c 2 = AP mode started (non-blocking).
 */
char WiFiBootSequence();

/**
 * @brief Service captive-portal DNS and AP auto-restart timer.
 *
 * No-op in STA mode.  Resets the AP idle timeout while a station is
 * associated.  Restarts the device to retry STA after @c AP_TIMEOUT_MS
 * (5 min) when stored credentials exist.
 *
 * Call every loop iteration.
 */
void WiFiAPProcess();

/** @return @c true when the device is operating as a WiFi access point. */
bool   WiFiIsAPMode();

/** @return Current STA IP address as a dotted-decimal string, e.g. @c "192.168.1.42". */
String GetIP();

/** @return Device MAC address string, e.g. @c "AA:BB:CC:DD:EE:FF". */
String GetMAC();

/** @return WiFi RSSI as a decimal string (STA mode only). */
String GetRSSIStr();

/** @brief Update the NTP client (no-op when WiFi is not connected). */
void   UpdateTime();

/** @brief Legacy stub — no-op. */
void   PrintTime();

/**
 * @brief Check whether the current NTP time matches a given hour and minute.
 * @param hour   Hour to match (0–23).
 * @param minute Minute to match (0–59).
 * @return @c 1 if matched, @c 0 if not, @c -1 if NTP time is not yet available.
 */
char   CheckTime(char hour, char minute);

/** @return @c 1 when WiFi STA is connected, @c 0 otherwise. */
char   GetWiFiStatus();

#endif /* WIFI_H */
