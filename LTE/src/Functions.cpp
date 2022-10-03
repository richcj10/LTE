#include "Functions.h"
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
unsigned long LTEPreviousMillis = 0;
unsigned long DebugPreviousMillis = 0;
unsigned long LTETestPreviousMillis = 0;

char Startup(bool WifiEnable, bool LTEEnable){
    ConfigIO();
    NetworkStop();
    UniqueName();
    FileStstemStart();
    LEDsetup();
    FGsetup(0);
    if(WifiEnable){
      WiFiNetworkSetup();
    }
    if(LTEEnable){
      LTEsetup();
    }
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

String GetUniqueName(){
  return UN;
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

void LTELoop(){
  if (millis() - LTEPreviousMillis >= LTE_LOOP) {
    LTEPreviousMillis = millis();
    LTEloop();
  }
  if (millis() - LTETestPreviousMillis >= LTE_Test_LOOP) {
    LTETestPreviousMillis = millis();
    NetworkTest();
  }
}

void DebugPrint(){
  if (millis() - DebugPreviousMillis >= DEBUG_LOOP) {
    DebugPreviousMillis = millis();
    FGDisplay();
    CellularDisplay();
  }
}