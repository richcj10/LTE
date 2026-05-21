#ifndef LOG_H
#define LOG_H

#define ERROR  1
#define LOG    2
#define NOTIFY 3
#define DEBUG  4

void LogSetup(char DebugLevel, bool WebPage);
char Log(char level, const char* format, ...);

/* Enable/disable serial (UART0) output. Disable when RS485/Modbus is active
   to prevent debug bytes from corrupting the bus. WebSocket log is unaffected. */
void SetSerialLog(bool enable);
bool GetSerialLog();

#endif 