#ifndef RS485_H
#define RS485_H

#include <Arduino.h>

/* ── Input register indices (master reads, LTE app writes) ────────────────
   Values are scaled integers — see comments for scale factor.            */
#define RS485_INP_VERSION       0   /* Protocol/firmware version (fixed = 2) */
#define RS485_INP_DEVICE_ID     1   /* Device type ID          (fixed = 2) */
#define RS485_INP_BATT_SOC      2   /* Battery SoC ×100  (7523 = 75.23%)  */
#define RS485_INP_BATT_V        3   /* Battery voltage ×100 (370 = 3.70V) */
#define RS485_INP_BATT_RATE     4   /* Charge rate ×10 %/hr (signed int16) */
#define RS485_INP_BATT_MODE     5   /* 1=AC, 2=Battery                     */
#define RS485_INP_CELL_RSSI     6   /* LTE RSSI magnitude (85 = -85 dBm)  */
#define RS485_INP_CELL_STATUS   7   /* 0=unreg 1=home 2=search 3=deny 4=unk 5=roam */
#define RS485_INP_WIFI_RSSI     8   /* WiFi RSSI magnitude                 */
#define RS485_INP_GPS_FIX       9   /* 1=valid fix, 0=no fix               */
#define RS485_INP_GPS_LAT_DEG   10  /* Latitude integer degrees            */
#define RS485_INP_GPS_LAT_DEC   11  /* Latitude decimal ×10000             */
#define RS485_INP_GPS_LON_DEG   12  /* Longitude integer degrees           */
#define RS485_INP_GPS_LON_DEC   13  /* Longitude decimal ×10000            */
#define RS485_INP_GPS_SPEED     14  /* Speed ×10 km/h                      */

#define RS485_DEVICE_VERSION    2   /* Increment when register map changes */
#define RS485_DEVICE_ID         3   /* Device type identifier (LTE-Monitor) */

/* ── Holding register user indices (master can read/write) ────────────── */
#define RS485_HOLD_FLAGS        0   /* Bit 0: serial debug enable            */
#define RS485_HOLD_MSG_0        1   /* First of 20 message registers         */
#define RS485_HOLD_MSG_LEN     20   /* 20 regs × 2 bytes = 40 chars max      */
                                    /* Packing: high byte = char[2n],        */
                                    /*          low  byte = char[2n+1]       */
                                    /* Null-terminate unused bytes with 0x00 */

/* ── Coil indices (master can read/write) ────────────────────────────── */
#define RS485_COIL_CELL_ENABLE  0   /* 1=cellular enabled                    */
#define RS485_COIL_GPS_ENABLE   1   /* 1=GPS polling enabled                 */
#define RS485_COIL_SEND_MSG     2   /* Master pulses 1 → slave sends Pushover*/

/* ── Discrete input indices (master reads, LTE app writes) ───────────── */
#define RS485_DI_WIFI_OK        0   /* WiFi connected */
#define RS485_DI_MQTT_OK        1   /* MQTT connected */
#define RS485_DI_CELL_OK        2   /* Cellular registered */
#define RS485_DI_GPS_FIX        3   /* GPS has fix */
#define RS485_DI_LOW_BATT       4   /* Battery alert */

void RS485setup();
void RS485loop();

uint8_t      RS485slaveId();
bool         RS485cellEnabled();
bool         RS485gpsEnabled();
unsigned int RS485getMsgCount();
String       RS485getEvents();

#endif
