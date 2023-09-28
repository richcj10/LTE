#ifndef WIFI_H
#define  WIFI_H

#include <Arduino.h>

char WiFiSetup();
String GetIP();
String GetMAC();
String GetRSSIStr();
void UpdateTime();
void PrintTime();
char CheckTime(char hour,char minute);
char GetWiFiStatus();

#endif 