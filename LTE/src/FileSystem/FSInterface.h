#ifndef FSINTERFACE_H
#define FSINTERFACE_H
#include "Arduino.h"

char FileStstemStart();   /* returns 1=ok, 2=AP mode needed, 0=error */
bool NeedsAPMode();

/* WiFi config accessors */
unsigned char GetWiFiMode();
String GetSSID();
String GetSSIDPassword();
String GetHostName();

/* MQTT config accessors */
unsigned char GetMQTTEnabled();
String        GetMQTTIP();
unsigned int  GetMQTTPort();
String        GetMQTTUser();
String        GetMQTTPassword();

bool GetRS485Mode();
void SetRS485Mode(bool on);

/* Pushover config accessors */
unsigned char GetPushoverEnabled();
String        GetPushoverToken();
String        GetPushoverUserKey();
void          SetPushoverConfig(unsigned char enabled, const char* token, const char* userKey);

/* Called by AP-mode submit handler to persist new credentials */
void SaveWifiToNVS(const char* ssid, const char* pass, const char* host);
void SaveMQTTToNVS(const char* ip, unsigned int port, const char* user, const char* pass, unsigned char enabled);

#endif