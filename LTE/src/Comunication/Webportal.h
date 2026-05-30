/**
 * @file Webportal.h
 * @brief Async HTTP + WebSocket web portal.
 *
 * Serves @c Main.html from LittleFS on port 80.  Provides a REST API under
 * @c /api/ for live telemetry and device control, and configuration endpoints
 * under @c /config/.  Log entries are pushed to connected browsers via a
 * WebSocket at @c /ws.
 *
 * API routes exposed by WebStart():
 *   - GET  /api/status           — device summary (name, IP, SoC, RSSI, etc.)
 *   - GET  /api/cellular         — LTE modem state
 *   - GET  /api/gps              — GPS fix and position
 *   - POST /api/gps/enable       — enable or disable the GNSS receiver
 *   - GET  /api/battery          — fuel-gauge readings
 *   - GET  /api/rs485            — Modbus slave stats
 *   - GET  /api/rs485/events     — event ring-buffer (newline-delimited text)
 *   - POST /api/rs485/mode       — toggle RS-485 mode (triggers reboot)
 *   - POST /api/debug/serial     — toggle UART0 serial debug output
 *   - POST /api/reboot           — reboot the device
 *   - GET/POST /config/wifi      — WiFi SSID and passphrase
 *   - GET/POST /config/mqtt      — MQTT broker settings
 *   - GET/POST /config/pushover  — Pushover credentials
 *   - GET/POST /config/cellular  — heartbeat interval
 *   - GET  /api/pushover/status  — last Pushover outcome
 *   - POST /api/pushover/test    — queue a test notification
 */
#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include <WiFi.h>
#include <Arduino.h>

/** @brief Register all HTTP/WebSocket routes and start the async web server.
 *  Call once during startup after WiFi is up. */
void WebStart();

/**
 * @brief Service periodic web portal tasks.
 *
 * Sends a deferred Pushover test notification if one is pending and cleans up
 * stale WebSocket clients.  Call from the main loop every iteration.
 */
void WebHandel();

/* Legacy synchronous HTTP helpers — not used with the async server */
void httpResponseRedirect(WiFiClient c);
void httpResponseHome(WiFiClient c);
void processCommand(char* command);
void httpResponse414(WiFiClient c);

/**
 * @brief Broadcast a log string to all connected WebSocket clients.
 * @param LogString The log message to send (will be wrapped in a JSON envelope).
 * @return @c 1 if the message was sent; @c 0 if no client is connected.
 */
char WebLogSend(String LogString);

#endif /* WEBPORTAL_H */
