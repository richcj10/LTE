#include <Arduino.h>
#include "Functions.h"
#include "Hardware/IO.h"
#include "Hardware/Log.h"
#include "Comunication/Webportal.h"

unsigned long previousMillis = 0;        // will store last time LED was updated

// constants won't change:
const long interval = 00;           // interval at which to blink (milliseconds)


void setup() {\
  LogSetup(DEBUG,1);
  Startup(1,1);
  Log(NOTIFY,"Started Main Program, took %lu mS",millis());

  //Log(ERROR,"Work Test %i", 1);
  //Log(LOG,"Work Test %i", 2);
  //Log(NOTIFY,"Work Test %i", 3);
  //Log(DEBUG,"Work Test %i", 4);
  //LEDUpdate(50);
  //RunLoop();
  //LTEloop();
}

void loop(){
  RunLoop();
  LTELoop();
  DebugPrint();
  // put your main code here, to run repeatedly:
}