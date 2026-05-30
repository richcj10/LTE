/**
 * @file RS485.cpp
 * @brief Modbus RTU slave — input register updates, master-write handling, and event ring-buffer.
 *
 * Slave address and RE/DE pin are injected at compile time via build flags:
 *   -DMBBP_SLAVE_ADDR=20  -DMBBP_DIR_PIN=4
 */
#include "RS485.h"
#include "Log.h"
#include "FuelGauge.h"
#include "cellular.h"
#include "../Define.h"
#include "../Comunication/Wifi.h"
#include "../Comunication/MQTT.h"
#include "ModBusBL.h"
#include <WiFi.h>

static ModBusBL modbus;

/** @defgroup rs485_internal Internal event ring-buffer
 *  24-entry circular buffer of human-readable event strings for the web portal.
 *  @{
 */
#define RS485_EVT_MAX 24
static String       _events[RS485_EVT_MAX];
static uint8_t      _evHead   = 0;
static uint8_t      _evCount  = 0;
static unsigned int _msgCount = 0;
/** @} */

/**
 * @brief Push a string into the event ring-buffer and increment the message counter.
 * @param s Event description string.
 */
static void pushEvent(const String& s) {
    _events[_evHead] = s;
    _evHead = (_evHead + 1) % RS485_EVT_MAX;
    if (_evCount < RS485_EVT_MAX) _evCount++;
    _msgCount++;
}

/**
 * @brief Initialise the Modbus slave on UART0 at 38400 baud.
 *
 * Also disables UART0 serial debug output to prevent bus corruption.
 */
void RS485setup() {
    SetSerialLog(false);
    modbus.begin(38400, Serial);
    Log(LOG, "RS485 slave started, ID=%d\n", modbus.slaveId());
}

/**
 * @brief Service one Modbus frame and synchronise all registers with device state.
 *
 * Input registers and discrete inputs are written from live sensor data before
 * calling modbus.update().  After update(), any coil or holding-register writes
 * from the master are detected and acted upon: GPS enable/disable, serial debug
 * toggle, and the Pushover trigger coil.
 */
void RS485loop() {
    /* ── Input registers ─────────────────────────────────────────── */
    modbus.setInput(RS485_INP_VERSION,     RS485_DEVICE_VERSION);
    modbus.setInput(RS485_INP_DEVICE_ID,   RS485_DEVICE_ID);
    modbus.setInput(RS485_INP_BATT_SOC,    (uint16_t)(GetCellSoC()   * 100.0f));
    modbus.setInput(RS485_INP_BATT_V,      (uint16_t)(GetCellV()     * 100.0f));
    modbus.setInput(RS485_INP_BATT_RATE,   (uint16_t)((int16_t)(GetCellRate() * 10.0f)));
    modbus.setInput(RS485_INP_BATT_MODE,   (uint16_t)GetPowerMode());
    modbus.setInput(RS485_INP_CELL_RSSI,   (uint16_t)abs(CellSig()));
    modbus.setInput(RS485_INP_CELL_STATUS, (uint16_t)0);
    modbus.setInput(RS485_INP_WIFI_RSSI,   (uint16_t)abs(WiFi.RSSI()));

    /* Encode LTE status as 0–5 */
    String cs = CellStatString();
    uint16_t cellStat = 0;
    if      (cs.indexOf("home")    >= 0) cellStat = 1;
    else if (cs.indexOf("Search")  >= 0) cellStat = 2;
    else if (cs.indexOf("Denied")  >= 0) cellStat = 3;
    else if (cs.indexOf("Unknown") >= 0) cellStat = 4;
    else if (cs.indexOf("oam")     >= 0) cellStat = 5;
    modbus.setInput(RS485_INP_CELL_STATUS, cellStat);

    /* GPS position — split lat/lon into integer and decimal parts */
    bool fix = GPShasFix();
    modbus.setInput(RS485_INP_GPS_FIX, (uint16_t)fix);
    if (fix) {
        float lat = GPSlat(), lon = GPSlon();
        modbus.setInput(RS485_INP_GPS_LAT_DEG, (uint16_t)(int)lat);
        modbus.setInput(RS485_INP_GPS_LAT_DEC, (uint16_t)((lat - (int)lat) * 10000.0f));
        modbus.setInput(RS485_INP_GPS_LON_DEG, (uint16_t)(int)lon);
        modbus.setInput(RS485_INP_GPS_LON_DEC, (uint16_t)((lon - (int)lon) * 10000.0f));
        modbus.setInput(RS485_INP_GPS_SPEED,   (uint16_t)(GPSspeed() * 10.0f));
    }

    /* ── Discrete inputs ─────────────────────────────────────────── */
    modbus.setDI(RS485_DI_WIFI_OK,  GetWiFiStatus() == 1);
    modbus.setDI(RS485_DI_MQTT_OK,  GetMQTTStatus() == 1);
    modbus.setDI(RS485_DI_CELL_OK,  cellStat == 1 || cellStat == 5);
    modbus.setDI(RS485_DI_GPS_FIX,  GPShasFix());
    modbus.setDI(RS485_DI_LOW_BATT, GetCellAlert());

    /* Snapshot writable state before processing so we can detect changes */
    bool     prevCellEn  = modbus.getCoil(RS485_COIL_CELL_ENABLE);
    bool     prevGpsEn   = modbus.getCoil(RS485_COIL_GPS_ENABLE);
    bool     prevSendMsg = modbus.getCoil(RS485_COIL_SEND_MSG);
    uint16_t prevFlags   = modbus.getHolding(RS485_HOLD_FLAGS);

    modbus.update();

    /* ── Detect and log master writes ────────────────────────────── */
    bool     newCellEn  = modbus.getCoil(RS485_COIL_CELL_ENABLE);
    bool     newGpsEn   = modbus.getCoil(RS485_COIL_GPS_ENABLE);
    bool     newSendMsg = modbus.getCoil(RS485_COIL_SEND_MSG);
    uint16_t newFlags   = modbus.getHolding(RS485_HOLD_FLAGS);

    if (newCellEn != prevCellEn)
        pushEvent(String("Cell enable: ") + (newCellEn ? "ON" : "OFF"));
    if (newGpsEn != prevGpsEn)
        pushEvent(String("GPS enable: ") + (newGpsEn ? "ON" : "OFF"));
    if (newFlags != prevFlags)
        pushEvent(String("Flags written: 0x") + String(newFlags, HEX) +
                  (newFlags & 0x01 ? " (serial debug ON)" : " (serial debug OFF)"));

    /* ── Pushover trigger: rising edge on COIL_SEND_MSG ────────────
       Unpack 20 holding registers (high byte = even char, low = odd char)
       into a 40-char message string, then queue it as a Pushover notification. */
    if (newSendMsg && !prevSendMsg) {
        char msgBuf[41] = {0};
        for (uint8_t i = 0; i < RS485_HOLD_MSG_LEN; i++) {
            uint16_t reg = modbus.getHolding(RS485_HOLD_MSG_0 + i);
            msgBuf[i * 2]     = (char)(reg >> 8);
            msgBuf[i * 2 + 1] = (char)(reg & 0xFF);
        }
        msgBuf[40] = '\0';
        Log(NOTIFY, "RS485 msg → Pushover: %s\n", msgBuf);
        pushEvent(String("Msg: ") + msgBuf);
        Pushover("RS485 Message", msgBuf);
        modbus.setCoil(RS485_COIL_SEND_MSG, false);
    }

    /* Apply serial-debug flag from bit 0 of holding register 0 */
    SetSerialLog(newFlags & 0x01);
}

/** @return Total Pushover messages triggered via COIL_SEND_MSG since boot. */
unsigned int RS485getMsgCount() { return _msgCount; }

/**
 * @brief Return the event ring-buffer as an oldest-first newline-delimited string.
 * @return Up to 24 event strings separated by newline characters.
 */
String RS485getEvents() {
    String out;
    uint8_t start = (_evCount < RS485_EVT_MAX) ? 0 : _evHead;
    for (uint8_t i = 0; i < _evCount; i++) {
        out += _events[(start + i) % RS485_EVT_MAX];
        out += '\n';
    }
    return out;
}

/** @return Configured Modbus slave address. */
uint8_t RS485slaveId()    { return modbus.slaveId(); }

/** @return @c true when COIL_CELL_ENABLE is set by the master. */
bool    RS485cellEnabled() { return modbus.getCoil(RS485_COIL_CELL_ENABLE); }

/** @return @c true when COIL_GPS_ENABLE is set by the master. */
bool    RS485gpsEnabled()  { return modbus.getCoil(RS485_COIL_GPS_ENABLE); }
