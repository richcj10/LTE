/**
 * @file IO.cpp
 * @brief GPIO, I²C, and UART0 initialisation plus debug LED helpers.
 */
#include "IO.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "Define.h"

static bool LED = 0;

/** @brief Configure GPIO directions, start UART0 at 115200, and start I²C on SDA=27/SCL=26. */
void ConfigIO() {
    pinMode(DEBUG_LED, OUTPUT);
    Serial.begin(115200);
    Wire.begin(27, 26);
}

/**
 * @brief Drive the debug LED to a fixed state.
 * @param value 1 = on, 0 = off.
 */
void DebugLED(char value) {
  if (value == 1) {
    LED = 1;
    digitalWrite(DEBUG_LED, HIGH);
  } else {
    LED = 0;
    digitalWrite(DEBUG_LED, LOW);
  }
}

/** @brief Toggle the debug LED state. */
void DebugLEDToggle() {
  LED = !LED;
  digitalWrite(DEBUG_LED, LED);
}
