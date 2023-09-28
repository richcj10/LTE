#include "LED.h"
#include <FastLED.h>
#include "Define.h"

char Brightness = 50;
char color = 0;
char Mode = LED_DEFAULT;
char Stat = 0;
long LEDRefreshRate = 0;
long LEDUpdateInterval = 500;
long SetLEDUpdate = 0;
unsigned long LEDcurrentMillis = 0;


// Define the array of leds
CRGB leds[NUM_LEDS];

void LEDsetup() {
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed
}

void LEDColor(char Value) {
  color = Value;
  FastLED.showColor(CHSV(color, 255, Brightness)); 
}

void LEDUpdate(){
  LEDcurrentMillis = millis();
  if (LEDcurrentMillis - LEDRefreshRate >= LEDUpdateInterval) {
    // save the last time you blinked the LED
    //Log(DEBUG,"LED Update, Mode = %d\r\n",Mode);
    LEDRefreshRate = LEDcurrentMillis;
    switch (Mode){
      case LED_DEFAULT:
        LEDUpdateInterval = SetLEDUpdate;
        FastLED.showColor(CHSV(color, 255, Brightness));
        break;
      case LED_BLINK:
        if(Stat == 1){
          LEDUpdateInterval = SetLEDUpdate;
          FastLED.showColor(CHSV(0, 0, 0)); 
          Stat = 0;
        }
        else{
          Stat = 1;
          FastLED.showColor(CHSV(color, 255, Brightness)); 
        }
        break;
      case LED_FLASH:
        if(Stat == 1){
          FastLED.showColor(CHSV(0, 0, 0)); 
          Stat = 0;
          LEDUpdateInterval = SetLEDUpdate;
        }
        else{
          Stat = 1;
          FastLED.showColor(CHSV(color, 255, Brightness));
          LEDUpdateInterval = 50;
        }
        break;
    }
  }
}

void SetLEDStatus(char type){
  Mode = type;
  LEDUpdate();
}

void SetLEDStatus(char type, int rate){
  Mode = type;
  SetLEDUpdate = rate;
  LEDUpdate();
}

char GetLEDMode(void){
  return Mode;
}

char GetStatus(){
    return 0;
}

void LEDBrightness(char Value) {
  Brightness = Value;
  FastLED.showColor(CHSV(color, 255, Brightness)); 
}

char LEDGetValue(){
  return color;
}