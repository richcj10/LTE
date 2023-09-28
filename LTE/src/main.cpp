#include <Arduino.h>
#include "Functions.h"
#include "Hardware/IO.h"
#include "Hardware/Log.h"
#include "Comunication/Webportal.h"
#include "Hardware/cellular.h"
#include "Hardware/LED.h"

unsigned long previousMillis = 0;        // will store last time LED was updated

// constants won't change:
const long interval = 00;           // interval at which to blink (milliseconds)


void setup() {
  LogSetup(DEBUG,1);
  Startup(1,1);
  Log(NOTIFY,"Started Main Program, took %lu mS\n\r",millis());

  //SendTextMsg();
  //LEDUpdate(50);
  //RunLoop();
  //LTEloop();
  LEDColor(96);
  LEDBrightness(10);
  SetLEDStatus(LED_FLASH,1000);
}

void loop(){
  RunLoop();
  LTELoop();
  DebugPrint();
  // put your main code here, to run repeatedly:
}