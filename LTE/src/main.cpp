#include <Arduino.h>
#include "Functions.h"
#include "Hardware/LED.h"
#include "Hardware/cellular.h"
#include "Hardware/Log.h"

unsigned long previousMillis = 0;        // will store last time LED was updated

// constants won't change:
const long interval = 50000;           // interval at which to blink (milliseconds)


void setup() {
  Startup();
  LogSetup(DEBUG);
  //delay(1000);
  Serial.print("Wifi");
  //WiFiSetup();
  Log(ERROR,"Work Test %i", 1);
  Log(LOG,"Work Test %i", 2);
  Log(NOTIFY,"Work Test %i", 3);
  Log(DEBUG,"Work Test %i", 4);
  //LEDUpdate(50);
  //LTEsetup();
  //RunLoop();
  //LTEloop();
}

char k = 0;

void loop(){
  //RunLoop();
  delay(500);
  DebugLED(1);
  delay(500);
  DebugLED(0);
  Log(ERROR,"Work Test %i", 1);
  Log(LOG,"Work Test %i", 2);
  Log(NOTIFY,"Work Test %i", 3);
  Log(DEBUG,"Work Test %i", 4);
  LogSetup(k++);
  if(k>4) k = 0;
 // DebugPrint();
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;

    //LTEloop();

  }
  //LEDloop();
  // put your main code here, to run repeatedly:
}