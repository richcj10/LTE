#include "WiFi.h"
#include <WiFi.h>
#include "Hardware/Log.h"
#include "Define.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "3.north-america.pool.ntp.org", 3600, 60000);

const char *soft_ap_password = "LTEAP";
const char* ssid = "Lights.Camera.Action";
const char* password = "RR58fa!8";
//const char* ssid = "EnovateEng";
//const char* password = "WirelessENG4u";

int WiFiTimeout = 0;
bool HaveWiFi = 0;
IPAddress CurrentIP;

char WiFiSetup(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    WiFiTimeout++;
    if(WiFiTimeout > WIFI_TIMEOUT){
      Log(ERROR,"Can not connect!");
      return -1;
    }
  }
  HaveWiFi = 1;
  CurrentIP = WiFi.localIP();
  timeClient.begin();
  Log(NOTIFY,"Connected to Wifi, The IP Address is :%d.%d.%d.%d\n", CurrentIP[0], CurrentIP[1], CurrentIP[2], CurrentIP[3]);
  return 1;
}

void UpdateTime(){
  timeClient.update();
}

void PrintTime(){
  Log(NOTIFY,"Time = %s\n",timeClient.getFormattedTime());
}

char CheckTime(char hour,char minute){
  if(timeClient.isTimeSet()) {
    if (hour == timeClient.getHours() && minute == timeClient.getMinutes()) {
      return 1;
    }
    else{
      return 0;
    }
  }
  else{
    return -1;
  } 
}

String GetIP(){
 return String(CurrentIP[0]) + String(".") +\
  String(CurrentIP[1]) + String(".") +\
  String(CurrentIP[2]) + String(".") +\
  String(CurrentIP[3]);
}

String GetMAC(){
  String Mac = WiFi.macAddress();
  return Mac;
}

String GetRSSIStr(){
  String RssiStr = String(WiFi.RSSI());
  return RssiStr;
}