/**
 * @file LED.cpp
 * @brief NeoPixel animation state machine — DEFAULT / BLINK / FLASH modes.
 *
 * A single WS2812B on DATA_PIN is driven via FastLED.  LEDUpdate() advances
 * the state machine; it must be called from the main loop every iteration.
 */
#include "LED.h"
#include <FastLED.h>
#include "Define.h"

static char  Brightness       = 50;
static char  color            = 0;
static char  Mode             = LED_DEFAULT;
static char  Stat             = 0;
static long  LEDRefreshRate   = 0;
static long  LEDUpdateInterval = 500;
static long  SetLEDUpdate     = 0;
static unsigned long LEDcurrentMillis = 0;

CRGB leds[NUM_LEDS];

/** @brief Initialise FastLED and attach the NeoPixel strip to DATA_PIN. */
void LEDsetup() {
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}

/**
 * @brief Immediately display the LED at the given hue with the current brightness.
 * @param Value HSV hue (0–255).
 */
void LEDColor(char Value) {
  color = Value;
  FastLED.showColor(CHSV(color, 255, Brightness));
}

/**
 * @brief Advance the LED animation state machine.
 *
 * LED_DEFAULT: solid on.
 * LED_BLINK: toggle on/off every LEDUpdateInterval ms.
 * LED_FLASH: short 50 ms pulse then off for LEDUpdateInterval ms.
 */
void LEDUpdate() {
  LEDcurrentMillis = millis();
  if (LEDcurrentMillis - LEDRefreshRate >= LEDUpdateInterval) {
    LEDRefreshRate = LEDcurrentMillis;
    switch (Mode) {
      case LED_DEFAULT:
        LEDUpdateInterval = SetLEDUpdate;
        FastLED.showColor(CHSV(color, 255, Brightness));
        break;
      case LED_BLINK:
        if (Stat == 1) {
          LEDUpdateInterval = SetLEDUpdate;
          FastLED.showColor(CHSV(0, 0, 0));
          Stat = 0;
        } else {
          Stat = 1;
          FastLED.showColor(CHSV(color, 255, Brightness));
        }
        break;
      case LED_FLASH:
        if (Stat == 1) {
          FastLED.showColor(CHSV(0, 0, 0));
          Stat = 0;
          LEDUpdateInterval = SetLEDUpdate;
        } else {
          Stat = 1;
          FastLED.showColor(CHSV(color, 255, Brightness));
          LEDUpdateInterval = 50;
        }
        break;
    }
  }
}

/**
 * @brief Set the animation mode without changing the interval.
 * @param type LED_DEFAULT, LED_BLINK, or LED_FLASH.
 */
void SetLEDStatus(char type) {
  Mode = type;
  LEDUpdate();
}

/**
 * @brief Set the animation mode and interval.
 * @param type Animation mode constant.
 * @param rate Period in milliseconds.
 */
void SetLEDStatus(char type, int rate) {
  Mode = type;
  SetLEDUpdate = rate;
  LEDUpdate();
}

/** @return Current animation mode constant. */
char GetLEDMode(void) {
  return Mode;
}

/**
 * @brief Set LED brightness.
 * @param Value Brightness level (0–255).
 */
void LEDBrightness(char Value) {
  Brightness = Value;
  FastLED.showColor(CHSV(color, 255, Brightness));
}

/** @return Current hue value. */
char LEDGetValue() {
  return color;
}
