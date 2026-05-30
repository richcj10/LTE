/**
 * @file RS485.h
 * @brief Modbus RTU slave driver and register map for the LTE monitor node.
 *
 * The device presents itself as a Modbus slave (default address 20, set via
 * @c -DMBBP_SLAVE_ADDR in platformio.ini).  UART0 is shared with serial debug
 * output — SetSerialLog(false) is called in RS485setup() so that debug bytes
 * cannot corrupt the bus.  The RE/DE direction pin is RS485_DIR_PIN (GPIO 4).
 *
 * @note The register map version is RS485_DEVICE_VERSION.  Increment it whenever
 *       the layout changes so that masters can detect incompatible slaves.
 */
#ifndef RS485_H
#define RS485_H

#include <Arduino.h>

/** @defgroup rs485_input Input Registers — master reads, slave writes
 *
 *  All values are unsigned 16-bit integers.  Scale factors are noted per entry.
 *  @{
 */
#define RS485_INP_VERSION       0   /**< Protocol/firmware version (fixed = RS485_DEVICE_VERSION) */
#define RS485_INP_DEVICE_ID     1   /**< Device type identifier (fixed = RS485_DEVICE_ID) */
#define RS485_INP_BATT_SOC      2   /**< Battery SoC × 100  (7523 → 75.23 %) */
#define RS485_INP_BATT_V        3   /**< Battery voltage × 100  (370 → 3.70 V) */
#define RS485_INP_BATT_RATE     4   /**< Charge rate × 10 %/hr — signed int16 packed in uint16 */
#define RS485_INP_BATT_MODE     5   /**< Power source: 1 = AC, 2 = Battery */
#define RS485_INP_CELL_RSSI     6   /**< LTE RSSI magnitude (85 → –85 dBm) */
#define RS485_INP_CELL_STATUS   7   /**< 0=unregistered 1=home 2=searching 3=denied 4=unknown 5=roaming */
#define RS485_INP_WIFI_RSSI     8   /**< WiFi RSSI magnitude */
#define RS485_INP_GPS_FIX       9   /**< 1 = valid fix, 0 = no fix */
#define RS485_INP_GPS_LAT_DEG   10  /**< Latitude integer degrees */
#define RS485_INP_GPS_LAT_DEC   11  /**< Latitude fractional part × 10 000 */
#define RS485_INP_GPS_LON_DEG   12  /**< Longitude integer degrees */
#define RS485_INP_GPS_LON_DEC   13  /**< Longitude fractional part × 10 000 */
#define RS485_INP_GPS_SPEED     14  /**< Speed × 10 km/h */
/** @} */

#define RS485_DEVICE_VERSION    2   /**< Increment when the register layout changes */
#define RS485_DEVICE_ID         3   /**< Device type code: LTE-Monitor */

/** @defgroup rs485_holding Holding Registers — master can read/write
 *  @{
 */
#define RS485_HOLD_FLAGS        0   /**< Bit 0: serial debug enable (1 = on) */
#define RS485_HOLD_MSG_0        1   /**< First of 20 message registers (40 chars total) */
#define RS485_HOLD_MSG_LEN     20   /**< Number of message registers; high byte = char[2n], low byte = char[2n+1] */
/** @} */

/** @defgroup rs485_coils Coils — master can read/write
 *  @{
 */
#define RS485_COIL_CELL_ENABLE  0   /**< 1 = cellular stack should be enabled */
#define RS485_COIL_GPS_ENABLE   1   /**< 1 = GNSS receiver should be powered on */
#define RS485_COIL_SEND_MSG     2   /**< Master pulses this to 1 to trigger a Pushover notification from the message holding registers */
/** @} */

/** @defgroup rs485_di Discrete Inputs — master reads, slave writes
 *  @{
 */
#define RS485_DI_WIFI_OK        0   /**< WiFi is connected */
#define RS485_DI_MQTT_OK        1   /**< MQTT broker is connected */
#define RS485_DI_CELL_OK        2   /**< LTE is registered (home or roaming) */
#define RS485_DI_GPS_FIX        3   /**< GPS has a valid fix */
#define RS485_DI_LOW_BATT       4   /**< Battery low-alert threshold has been crossed */
/** @} */

/** @brief Initialise the Modbus slave at 38400 baud on UART0.
 *  Also calls SetSerialLog(false) to protect the bus from debug output. */
void RS485setup();

/**
 * @brief Service one Modbus frame and synchronise all registers with device state.
 *
 * Steps performed each call:
 * 1. Write all input registers from live sensor readings.
 * 2. Write discrete inputs from subsystem status flags.
 * 3. Snapshot writable state (coils, holding registers).
 * 4. Call modbus.update() to process one pending master request.
 * 5. Detect master writes and emit descriptive event-log entries.
 * 6. On rising edge of COIL_SEND_MSG, decode the message registers and queue a Pushover notification.
 * 7. Apply the serial-debug flag from holding register 0.
 *
 * Call from the main loop every RS485_UPDATE_LOOP ms.
 */
void RS485loop();

/** @return Configured Modbus slave address. */
uint8_t      RS485slaveId();
/** @return @c true when the master has COIL_CELL_ENABLE set. */
bool         RS485cellEnabled();
/** @return @c true when the master has COIL_GPS_ENABLE set. */
bool         RS485gpsEnabled();
/** @return Total number of Pushover messages triggered via the RS-485 message coil. */
unsigned int RS485getMsgCount();
/** @return Oldest-first newline-delimited event log (ring buffer, up to 24 entries). */
String       RS485getEvents();

#endif /* RS485_H */
