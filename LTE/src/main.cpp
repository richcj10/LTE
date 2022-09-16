#include <Arduino.h>
#include "Functions.h"
#include "Hardware/LED.h"

void setup() {
  Startup();
  WiFiSetup();
  LEDUpdate(50);
}

void loop(){
  RunLoop();
  delay(1000);
  DebugLED(1);
  delay(1000);
  DebugLED(0);
  DebugPrint();
  //LEDloop();
  // put your main code here, to run repeatedly:
}