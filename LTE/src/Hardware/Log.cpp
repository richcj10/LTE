#include "Log.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>
#include "Comunication/Webportal.h"

bool ConfigArray[5] = {1,1,1,1,0};
bool WiFiLog = 0;
bool SerialEnabled = false;  /* off by default — RS485 owns UART0 */

void SetSerialLog(bool enable) { SerialEnabled = enable; }
bool GetSerialLog() { return SerialEnabled; }

void LogSetup(char DebugLevel, bool WebPage){
    switch (DebugLevel){
        case ERROR:
            ConfigArray[0] = 1;
            ConfigArray[1] = 0;
            ConfigArray[2] = 0;
            ConfigArray[3] = 0;
            break;
        case LOG:
            ConfigArray[0] = 1;
            ConfigArray[1] = 1;
            ConfigArray[2] = 0;
            ConfigArray[3] = 0;
            break;
        case NOTIFY:
            ConfigArray[0] = 1;
            ConfigArray[1] = 1;
            ConfigArray[2] = 1;
            ConfigArray[3] = 0;
            break;
        case DEBUG:
            ConfigArray[0] = 1;
            ConfigArray[1] = 1;
            ConfigArray[2] = 1;
            ConfigArray[3] = 1;
            break;
    }
    if(WebPage){
        WiFiLog = 1;
    }
}

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
    va_list arg;
    va_list copy;
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

    if (SerialEnabled) {
        ets_printf("%s%s", prefix, temp);
    }
    if (WiFiLog) {
        String s = String(prefix) + temp;
        WebLogSend(s);
    }

    if (len >= (int)sizeof(loc_buf)) free(temp);
    return 0;
}