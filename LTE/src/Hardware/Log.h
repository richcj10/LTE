/**
 * @file Log.h
 * @brief Levelled logging with UART0 serial and WebSocket output.
 *
 * Output goes to UART0 when serial is enabled via SetSerialLog() and to the
 * web-portal WebSocket when enabled via LogSetup().  UART0 output is off by
 * default because RS-485/Modbus owns that UART; the web-portal RS-485 tab
 * provides a toggle to enable it temporarily for diagnostics.
 */
#ifndef LOG_H
#define LOG_H

/** @defgroup log_levels Log severity levels
 *  Higher values produce more verbose output.
 *  @{
 */
#define ERROR  1  /**< Critical errors — always shown when ERROR or higher is configured */
#define LOG    2  /**< General informational events */
#define NOTIFY 3  /**< Important operational notifications (state changes, boot progress) */
#define DEBUG  4  /**< Verbose diagnostic output */
/** @} */

/**
 * @brief Configure the log subsystem.
 * @param DebugLevel Maximum severity level to emit: ERROR, LOG, NOTIFY, or DEBUG.
 * @param WebPage    Pass @c true to forward log entries to the WebSocket log stream.
 */
void LogSetup(char DebugLevel, bool WebPage);

/**
 * @brief Emit a formatted log message at the given severity level.
 *
 * Thread-safe; uses a 64-byte stack buffer and falls back to heap allocation
 * for longer messages.  Both serial and WebSocket outputs are guarded by their
 * respective enable flags.
 *
 * @param level  One of ERROR, LOG, NOTIFY, or DEBUG.
 * @param format printf-style format string.
 * @param ...    Variadic format arguments.
 * @return Always @c 0.
 */
char Log(char level, const char* format, ...);

/**
 * @brief Enable or disable UART0 serial output.
 *
 * Must be set to @c false while RS-485/Modbus is active to prevent spurious
 * bytes on the bus.  WebSocket logging is unaffected by this setting.
 *
 * @param enable @c true to enable serial output; @c false to suppress it.
 */
void SetSerialLog(bool enable);

/** @return @c true if UART0 serial output is currently enabled. */
bool GetSerialLog();

#endif /* LOG_H */
