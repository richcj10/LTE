#ifndef Define_H
#define Define_H

#define UPDATE_LOOP       1000
#define MQTT_UPDATE_LOOP  10000
#define DEBUG_LOOP        10000
#define LTE_LOOP          30000
#define LTE_Test_LOOP     5000
#define RS485_UPDATE_LOOP 1000

#define NUM_LEDS  1
#define DATA_PIN  21
#define DEBUG_LED 2

#define SIMCOM_7000
#define FONA_PWRKEY 19
#define FONA_RST    18

/* RS-485: direction pin (RE/DE). TX/RX on UART0 default pins (GPIO1/GPIO3).
   Slave address default set via -DMBBP_SLAVE_ADDR=20 in platformio.ini. */
#define RS485_DIR_PIN 4

#define WIFI_TIMEOUT 15

#endif
