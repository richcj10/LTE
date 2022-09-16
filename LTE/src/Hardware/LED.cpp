#include "LED.h"
#include <FastLED.h>

#define NUM_LEDS 1

#define DATA_PIN 21

// Define the array of leds
CRGB leds[NUM_LEDS];

void LEDsetup() {
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);  // GRB ordering is assumed
}

void LEDloop() {
  // Turn the LED on, then pause
  leds[0] = CRGB::Red;
  FastLED.show();
  delay(500);
  // Now turn the LED off, then pause
  leds[0] = CRGB::Black;
  FastLED.show();
  delay(500);
}