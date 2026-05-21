#include "FSInterface.h"
#include "FileSystem.h"
#include "Arduino.h"

WiFiConfig     wfconfig;
MQTTConfig     mqconfig;
SystemConfig   sysconfig;
PushoverConfig pvconfig;

static bool _needsAP = false;

char FileStstemStart(){
    char result = FileSystemInit(&wfconfig, &mqconfig, &sysconfig, &pvconfig);
    _needsAP = (result == 2);
    return (result == 0) ? 0 : 1;
}

bool NeedsAPMode() { return _needsAP; }

unsigned char GetWiFiMode(){
    return wfconfig.WIFIMode;
}

String GetSSID(){
    char ReturnArray[wfconfig.SSIDLN];
    for(unsigned char i = 0;i<=wfconfig.SSIDLN;i++){
        ReturnArray[i] = wfconfig.SSID[i];
    }
    String ReturnString = String(ReturnArray);
    return ReturnString;
}

String GetSSIDPassword(){
    char ReturnArray[wfconfig.PswdLN];
    for(unsigned char i = 0;i<=wfconfig.PswdLN;i++){
        ReturnArray[i] = wfconfig.Passcode[i];
    }
    String ReturnString = String(ReturnArray);
    return ReturnString;
}

String GetHostName(){
    char ReturnArray[wfconfig.HoastLN];
    for(unsigned char i = 0;i<=wfconfig.HoastLN;i++){
        ReturnArray[i] = wfconfig.Host[i];
    }
    return String(ReturnArray);
}

unsigned char GetMQTTEnabled()  { return mqconfig.MQTTEnabble; }
String        GetMQTTIP()       { return String(mqconfig.MQTTIP); }
unsigned int  GetMQTTPort()     { return mqconfig.MQTTPort; }
String        GetMQTTUser()     { return String(mqconfig.MQTTUser); }
String        GetMQTTPassword() { return String(mqconfig.MQTTPassword); }

bool GetRS485Mode() { return sysconfig.rs485Mode; }
void SetRS485Mode(bool on) {
    sysconfig.rs485Mode = on;
    SystemsaveConfiguration(&sysconfig);
}

unsigned char GetPushoverEnabled()  { return pvconfig.enabled; }
String        GetPushoverToken()    { return String(pvconfig.token); }
String        GetPushoverUserKey()  { return String(pvconfig.userKey); }

void SetPushoverConfig(unsigned char enabled, const char* token, const char* userKey) {
    pvconfig.enabled = enabled;
    strlcpy(pvconfig.token,   token,   sizeof(pvconfig.token));
    strlcpy(pvconfig.userKey, userKey, sizeof(pvconfig.userKey));
    PushoversaveConfiguration(&pvconfig);
}

void SaveWifiToNVS(const char* ssid, const char* pass, const char* host) {
    strlcpy(wfconfig.SSID,     ssid, sizeof(wfconfig.SSID));
    wfconfig.SSIDLN = strlen(ssid);
    strlcpy(wfconfig.Passcode, pass, sizeof(wfconfig.Passcode));
    wfconfig.PswdLN = strlen(pass);
    strlcpy(wfconfig.Host,     host, sizeof(wfconfig.Host));
    wfconfig.HoastLN = strlen(host);
    _needsAP = false;
    NVSsaveWifi(&wfconfig);
}

void SaveMQTTToNVS(const char* ip, unsigned int port, const char* user, const char* pass, unsigned char enabled) {
    strlcpy(mqconfig.MQTTIP,       ip,   sizeof(mqconfig.MQTTIP));
    mqconfig.MQTTPort       = port;
    mqconfig.MQTTEnabble    = enabled;
    strlcpy(mqconfig.MQTTUser,     user, sizeof(mqconfig.MQTTUser));
    strlcpy(mqconfig.MQTTPassword, pass, sizeof(mqconfig.MQTTPassword));
    mqconfig.MQTTPasswordLN = strlen(pass);
    NVSsaveMQTT(&mqconfig);
}