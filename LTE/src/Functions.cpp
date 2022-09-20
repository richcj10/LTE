#include "Functions.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "Hardware/LED.h"
#include "Hardware/FuelGauge.h"
#include "Define.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

String UN = "";

const char *soft_ap_password = "LTEAP";
//const char* wifi_network_ssid = "Lights.Camera.Action";
//const char* wifi_network_password =  "RR58fa!8";
const char* ssid = "Lights.Camera.Action";
const char* password = "RR58fa!8";

void ConfigIO(){
    //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    pinMode(DEBUG_LED,OUTPUT);
    Serial.begin(115200);
    Wire.begin(27,26);
    UniqueName();
}

char Startup(){
    ConfigIO();
    LEDsetup();
    FGsetup(1);
    return 1;
}

void UniqueName(){
  String Mac = WiFi.macAddress();
  //Serial.println(Mac);
  int Len = Mac.length();
  UN = "ESPPLC-LTE-";
  UN = UN + Mac.charAt(Len - 5);
  UN = UN + Mac.charAt(Len - 4);
  UN = UN + Mac.charAt(Len - 3);
  UN = UN + Mac.charAt(Len - 2);
  UN = UN + Mac.charAt(Len-1);
  Serial.println();
  Serial.println(UN);
}

void WiFiSetup(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void RunLoop(){
    FGloop();
}

void DebugPrint(){
    FGDisplay();
}