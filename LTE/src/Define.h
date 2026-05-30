/**
 * @file Define.h
 * @brief Board-level hardware pin assignments and loop-timing constants.
 */
#ifndef Define_H
#define Define_H

/** @defgroup timing Loop-interval constants (milliseconds)
 *  @{
 */
#define UPDATE_LOOP       1000   /**< General-purpose update interval */
#define MQTT_UPDATE_LOOP  10000  /**< MQTT publish + WiFi watchdog interval */
#define DEBUG_LOOP        10000  /**< Periodic debug-print interval */
#define LTE_LOOP          30000  /**< LTE status-check interval */
#define LTE_Test_LOOP     5000   /**< LTE test-mode interval */
#define RS485_UPDATE_LOOP 1000   /**< RS-485 Modbus slave poll interval */
/** @} */

/** @defgroup gpio GPIO pin assignments
 *  @{
 */
#define NUM_LEDS  1   /**< Number of NeoPixel LEDs on the DATA_PIN strip */
#define DATA_PIN  21  /**< NeoPixel data pin (GPIO 21) */
#define DEBUG_LED 2   /**< On-board debug LED (GPIO 2) */

#define SIMCOM_7000       /**< Selects the SIM7000 variant in the Adafruit FONA library */
#define FONA_PWRKEY 19    /**< SIM7070G PWRKEY pin (GPIO 19) */
#define FONA_RST    18    /**< SIM7070G RST pin (GPIO 18) */

/** @brief RS-485 RE/DE direction-control pin.
 *  TX/RX are on UART0 default pins (GPIO 1/GPIO 3). */
#define RS485_DIR_PIN 4

/** @brief Legacy WiFi connect-timeout in seconds (not used in current boot sequence). */
#define WIFI_TIMEOUT 15
/** @} */

#endif /* Define_H */
