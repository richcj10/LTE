/**
 * @file main.cpp
 * @brief Arduino entry point — setup() and loop().
 *
 * Calls Startup() once to initialise all subsystems, then delegates every
 * loop iteration to RunLoop() (non-blocking dispatcher) and DebugPrint()
 * (timed diagnostic output).  The cellular stack runs independently on
 * Core 0 via its own FreeRTOS task started inside Startup().
 */
#include <Arduino.h>
#include "Functions.h"
#include "Hardware/IO.h"
#include "Hardware/Log.h"
#include "Comunication/Webportal.h"
#include "Hardware/cellular.h"
#include "Hardware/LED.h"

unsigned long previousMillis = 0;
const long interval = 00;

void setup() {
  LogSetup(NOTIFY, 1);
  Startup(1, 1);

  LEDColor(96);
  LEDBrightness(10);
  SetLEDStatus(LED_FLASH, 1000);
}

void loop() {
  RunLoop();
  DebugPrint();
}
