#include "IO.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "Define.h"

bool LED = 0;

void ConfigIO(){
    pinMode(DEBUG_LED,OUTPUT);
    Serial.begin(115200);
    Wire.begin(27,26);
}

void DebugLED(char value){
  if(value == 1){
    LED = 1;
    digitalWrite(DEBUG_LED, HIGH);
  }
  else{
    LED = 0;
    digitalWrite(DEBUG_LED, LOW);
  }
}

void DebugLEDToggle(){
  LED = !LED;
  digitalWrite(DEBUG_LED, LED);
}