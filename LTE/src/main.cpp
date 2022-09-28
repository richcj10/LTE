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
  delay(1000);
  Serial.print("Wifi");
  //WiFiSetup();
  Log(1,"Work Test %i", 1);
  //LEDUpdate(50);
  //LTEsetup();
  //RunLoop();
  //LTEloop();
}

void loop(){
  //RunLoop();
  delay(1000);
  DebugLED(1);
  delay(1000);
  DebugLED(0);
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