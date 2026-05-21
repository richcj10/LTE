#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

/* Blocking boot-time state machine.
   - Tries NVS creds for up to 30 s.
   - Falls back to AP mode ("LTE-Setup") for up to 5 min.
   - If user submits config → saves to NVS → restarts.
   - If AP times out → retries NVS. Loops forever until connected.
   Returns 1 when STA WiFi is up. */
char WiFiBootSequence();

String GetIP();
String GetMAC();
String GetRSSIStr();
void   UpdateTime();
void   PrintTime();
char   CheckTime(char hour, char minute);
char   GetWiFiStatus();

#endif