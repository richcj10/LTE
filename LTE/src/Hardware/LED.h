/**
 * @file LED.h
 * @brief NeoPixel RGB LED driver with DEFAULT / BLINK / FLASH animation modes.
 *
 * A single WS2812B LED on DATA_PIN (GPIO 21) is driven via FastLED.
 * Colour is expressed as an HSV hue (0–255); brightness is set separately.
 * The animation state machine is advanced by calling LEDUpdate() from the
 * main loop every iteration.
 */
#ifndef LED_H
#define LED_H

/** @defgroup led_modes LED animation modes
 *  @{
 */
#define LED_DEFAULT 10  /**< Solid on at the current hue and brightness */
#define LED_BLINK   11  /**< Alternate on/off at the configured interval */
#define LED_FLASH   12  /**< Short 50 ms flash followed by off for the configured interval */
/** @} */

/** @brief Initialise FastLED and attach the single NeoPixel to DATA_PIN. */
void LEDsetup();

/**
 * @brief Set LED brightness.
 * @param Value Brightness level (0 = off, 255 = maximum).
 */
void LEDBrightness(char Value);

/** @return Current hue (colour) value. */
char LEDGetValue();

/**
 * @brief Immediately display the LED at the given hue with the current brightness.
 * @param Value HSV hue (0–255).
 */
void LEDColor(char Value);

/**
 * @brief Advance the LED animation state machine.
 *
 * In LED_BLINK and LED_FLASH modes the LED state toggles at the interval
 * set by SetLEDStatus().  Must be called from the main loop every iteration.
 */
void LEDUpdate();

/**
 * @brief Set the animation mode without changing the current interval.
 * @param type LED_DEFAULT, LED_BLINK, or LED_FLASH.
 */
void SetLEDStatus(char type);

/**
 * @brief Set the animation mode and period.
 * @param type Animation mode constant (LED_DEFAULT, LED_BLINK, LED_FLASH).
 * @param rate Interval in milliseconds.
 */
void SetLEDStatus(char type, int rate);

/** @return Current animation mode constant. */
char GetLEDMode(void);

#endif /* LED_H */
