#include "LED.h"
#include <FastLED.h>
#include "Define.h"

char Brightness = 50;

char color = 0;

// Define the array of leds
CRGB leds[NUM_LEDS];

void LEDsetup() {
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed
}

void LEDUpdate(char Value) {
  color = Value;
  FastLED.showColor(CHSV(color, 255, Brightness)); 
}

void LEDBrightness(char Value) {
  Brightness = Value;
  FastLED.showColor(CHSV(color, 255, Brightness)); 
}

void DebugLED(char value){
  if(value == 1){
    digitalWrite(DEBUG_LED, HIGH);
  }
  else{
    digitalWrite(DEBUG_LED, LOW);
  }
}