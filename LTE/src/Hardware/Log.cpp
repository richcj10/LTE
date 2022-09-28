#include "Log.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>

void Log(char level,const char* format, ...){ 
    va_list args;
    Serial.println();
    switch (level)
    {
    case ERROR:
        Serial.print("ERR>");
        break;
    case LOG:
        Serial.print("LOG>");
        break;
     case NOTIFY:
        Serial.print("NOTIFY>");
        break;
    case DEBUG:
        Serial.print("DEBUG>");
        break;
    default:
        break;
    }
    printf("hi");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}