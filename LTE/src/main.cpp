#include <Arduino.h>
#include "Functions.h"
#include "Hardware/LED.h"

void setup() {
  Startup();
}

void loop() {
  RunLoop();
  delay(1000);
  DebugPrint();
  LEDloop();
  // put your main code here, to run repeatedly:
}