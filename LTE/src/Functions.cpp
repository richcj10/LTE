#include "Functions.h"
#include <Arduino.h>
#include <WiFi.h>
#include "Hardware/LED.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"
#include "Define.h"
#include "Hardware/Log.h"
#include "Hardware/IO.h"
#include "Comunication/Webportal.h"
#include "Comunication/Wifi.h"
#include "FileSystem/FSInterface.h"

String UN = "";

unsigned long UpdatePreviousMillis = 0;  
unsigned long DebugPreviousMillis = 0;

char Startup(){
    ConfigIO();
    NetworkStop();
    UniqueName();
    FileStstemStart();
    LEDsetup();
    FGsetup(0);
    return 1;
}

void WiFiNetworkSetup(){
  if(WiFiSetup() != -1){
    WebStart();
  }
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

void RunLoop(){
  WebHandel();
  if (millis() - UpdatePreviousMillis >= UPDATE_LOOP) {
    UpdatePreviousMillis = millis();
    DebugLEDToggle();
    FGloop();
    NetworkStatusUpdate();
  }
}

void DebugPrint(){
  if (millis() - DebugPreviousMillis >= DEBUG_LOOP) {
    DebugPreviousMillis = millis();
    FGDisplay();
    CellularDisplay();
  }
}