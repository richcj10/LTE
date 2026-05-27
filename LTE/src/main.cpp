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


//#define FONA_PWRKEY 19
//#define FONA_RST    18

void setup() {
  LogSetup(NOTIFY,1);
  Startup(1,1);
  //Log(NOTIFY,"Started Main Program, took %lu mS\n\r",millis());

  //SendTextMsg();
  //LEDUpdate(50);
  //RunLoop();
  //LTEloop();
  LEDColor(96);
  LEDBrightness(10);
  SetLEDStatus(LED_FLASH,1000);
/*    pinMode(FONA_RST,    OUTPUT);
  pinMode(FONA_PWRKEY, OUTPUT);
  digitalWrite(FONA_RST, HIGH); 
  digitalWrite(FONA_PWRKEY, HIGH); 
  delay(100);
  digitalWrite(FONA_PWRKEY, LOW);
  delay(1500);
  digitalWrite(FONA_PWRKEY, HIGH);  */
}

void loop(){
  RunLoop();
  DebugPrint();
  // put your main code here, to run repeatedly:
}