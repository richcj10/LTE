/**
 * @file Log.cpp
 * @brief Levelled log implementation — UART0 serial and WebSocket output.
 *
 * Levels are enabled up to the threshold set by LogSetup().  Serial output
 * uses ets_printf() (ISR-safe) and is gated by SerialEnabled.  WebSocket
 * output is forwarded via WebLogSend().  Serial is disabled by default
 * because RS-485/Modbus owns UART0.
 */
#include "Log.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>
#include "Comunication/Webportal.h"

static bool ConfigArray[5] = {1, 1, 1, 1, 0};
static bool WiFiLog        = 0;
static bool SerialEnabled  = false;

/** @brief Enable or disable UART0 serial output. */
void SetSerialLog(bool enable) { SerialEnabled = enable; }

/** @return @c true if UART0 serial output is currently enabled. */
bool GetSerialLog() { return SerialEnabled; }

/**
 * @brief Configure log level thresholds and optional WebSocket forwarding.
 * @param DebugLevel Maximum level to emit (ERROR, LOG, NOTIFY, or DEBUG).
 * @param WebPage    @c true to forward entries to the WebSocket log stream.
 */
void LogSetup(char DebugLevel, bool WebPage) {
    switch (DebugLevel) {
        case ERROR:
            ConfigArray[0] = 1; ConfigArray[1] = 0;
            ConfigArray[2] = 0; ConfigArray[3] = 0;
            break;
        case LOG:
            ConfigArray[0] = 1; ConfigArray[1] = 1;
            ConfigArray[2] = 0; ConfigArray[3] = 0;
            break;
        case NOTIFY:
            ConfigArray[0] = 1; ConfigArray[1] = 1;
            ConfigArray[2] = 1; ConfigArray[3] = 0;
            break;
        case DEBUG:
            ConfigArray[0] = 1; ConfigArray[1] = 1;
            ConfigArray[2] = 1; ConfigArray[3] = 1;
            break;
    }
    if (WebPage) WiFiLog = 1;
}

/**
 * @brief Format and emit a log message.
 *
 * Uses a 64-byte stack buffer; falls back to heap for longer messages.
 * Serial output is prefixed with the level tag (ERR>, LOG>, NOTFY>, DEBUG>).
 *
 * @param level  One of ERROR, LOG, NOTIFY, DEBUG.
 * @param format printf-style format string.
 * @return Always @c 0.
 */
char Log(char level, const char* format, ...) {
    bool levelEnabled = false;
    const char* prefix = "ALT>";

    switch (level) {
        case ERROR:  levelEnabled = ConfigArray[0]; prefix = "ERR>";   break;
        case LOG:    levelEnabled = ConfigArray[1]; prefix = "LOG>";   break;
        case NOTIFY: levelEnabled = ConfigArray[2]; prefix = "NOTFY>"; break;
        case DEBUG:  levelEnabled = ConfigArray[3]; prefix = "DEBUG>"; break;
        default: break;
    }

    if (!levelEnabled && level != 0) return 0;

    static char loc_buf[64];
    char* temp = loc_buf;
    int len;
    va_list arg, copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (len >= (int)sizeof(loc_buf)) {
        temp = (char*)malloc(len + 1);
        if (temp == NULL) { va_end(arg); return 0; }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);

    if (SerialEnabled) ets_printf("%s%s", prefix, temp);
    if (WiFiLog)       WebLogSend(String(prefix) + temp);

    if (len >= (int)sizeof(loc_buf)) free(temp);
    return 0;
}
