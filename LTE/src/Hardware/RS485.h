#ifndef RS485_H
#define RS485_H

#include <Arduino.h>

/* ── Input register indices (master reads, LTE app writes) ────────────────
   Values are scaled integers — see comments for scale factor.            */
#define RS485_INP_BATT_SOC      0   /* Battery SoC ×100  (7523 = 75.23%) */
#define RS485_INP_BATT_V        1   /* Battery voltage ×100 (370 = 3.70V) */
#define RS485_INP_BATT_RATE     2   /* Charge rate ×10 %/hr (signed int16) */
#define RS485_INP_BATT_MODE     3   /* 1=AC, 2=Battery */
#define RS485_INP_CELL_RSSI     4   /* LTE RSSI magnitude (85 = -85 dBm) */
#define RS485_INP_CELL_STATUS   5   /* 0=unreg 1=home 2=search 3=deny 4=unk 5=roam */
#define RS485_INP_WIFI_RSSI     6   /* WiFi RSSI magnitude */
#define RS485_INP_GPS_FIX       7   /* 1=valid fix, 0=no fix */
#define RS485_INP_GPS_LAT_DEG   8   /* Latitude integer degrees */
#define RS485_INP_GPS_LAT_DEC   9   /* Latitude decimal ×10000 */
#define RS485_INP_GPS_LON_DEG   10  /* Longitude integer degrees */
#define RS485_INP_GPS_LON_DEC   11  /* Longitude decimal ×10000 */
#define RS485_INP_GPS_SPEED     12  /* Speed ×10 km/h */

/* ── Holding register user indices (master can read/write) ────────────── */
#define RS485_HOLD_FLAGS        0   /* Bit 0: serial debug enable */

/* ── Coil indices (master can read/write) ────────────────────────────── */
#define RS485_COIL_CELL_ENABLE  0   /* 1=cellular enabled */
#define RS485_COIL_GPS_ENABLE   1   /* 1=GPS polling enabled */

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
