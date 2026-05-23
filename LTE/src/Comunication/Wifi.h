#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

/* Tries NVS credentials for up to 30 s.
   Returns 1 = STA connected, 2 = AP mode started (non-blocking).
   In AP mode, call WiFiAPProcess() from the main loop. */
char WiFiBootSequence();

/* Must be called every loop iteration. No-op when in STA mode.
   Processes captive-portal DNS and restarts after AP_TIMEOUT_MS
   if stored credentials exist. */
void WiFiAPProcess();

bool   WiFiIsAPMode();
String GetIP();
String GetMAC();
String GetRSSIStr();
void   UpdateTime();
void   PrintTime();
char   CheckTime(char hour, char minute);
char   GetWiFiStatus();

#endif