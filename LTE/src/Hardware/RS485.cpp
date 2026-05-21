#include "RS485.h"
#include "Log.h"
#include "FuelGauge.h"
#include "cellular.h"
#include "../Define.h"
#include "../Comunication/Wifi.h"
#include "../Comunication/MQTT.h"
#include "ModBusBL.h"
#include <WiFi.h>

/* Slave address and dir pin come from build_flags in platformio.ini:
     -DMBBP_SLAVE_ADDR=20  -DMBBP_DIR_PIN=4                           */
static ModBusBL modbus;

/* ── Event ring buffer ───────────────────────────────────────────────────── */
#define RS485_EVT_MAX 24
static String        _events[RS485_EVT_MAX];
static uint8_t       _evHead  = 0;
static uint8_t       _evCount = 0;
static unsigned int  _msgCount = 0;

static void pushEvent(const String& s) {
    _events[_evHead] = s;
    _evHead = (_evHead + 1) % RS485_EVT_MAX;
    if (_evCount < RS485_EVT_MAX) _evCount++;
    _msgCount++;
}

void RS485setup() {
    /* Serial (UART0) is owned by Modbus from this point forward.
       Serial debug output is disabled to avoid bus corruption.
       Enable via web portal toggle or RS485 holding register flag. */
    SetSerialLog(false);
    modbus.begin(38400, Serial);
    Log(LOG, "RS485 slave started, ID=%d\n", modbus.slaveId());
}

void RS485loop() {
    /* ── Push current device state into input registers ─────────────── */
    modbus.setInput(RS485_INP_BATT_SOC,    (uint16_t)(GetCellSoC()   * 100.0f));
    modbus.setInput(RS485_INP_BATT_V,      (uint16_t)(GetCellV()     * 100.0f));
    modbus.setInput(RS485_INP_BATT_RATE,   (uint16_t)((int16_t)(GetCellRate() * 10.0f)));
    modbus.setInput(RS485_INP_BATT_MODE,   (uint16_t)GetPowerMode());
    modbus.setInput(RS485_INP_CELL_RSSI,   (uint16_t)abs(CellSig()));
    modbus.setInput(RS485_INP_CELL_STATUS, (uint16_t)0);   /* updated below */
    modbus.setInput(RS485_INP_WIFI_RSSI,   (uint16_t)abs(WiFi.RSSI()));

    /* Cell status from string — encode as 0-5 */
    String cs = CellStatString();
    uint16_t cellStat = 0;
    if      (cs.indexOf("home")    >= 0) cellStat = 1;
    else if (cs.indexOf("Search")  >= 0) cellStat = 2;
    else if (cs.indexOf("Denied")  >= 0) cellStat = 3;
    else if (cs.indexOf("Unknown") >= 0) cellStat = 4;
    else if (cs.indexOf("oam")     >= 0) cellStat = 5;
    modbus.setInput(RS485_INP_CELL_STATUS, cellStat);

    /* GPS */
    bool fix = GPShasFix();
    modbus.setInput(RS485_INP_GPS_FIX,     (uint16_t)fix);
    if (fix) {
        float lat = GPSlat(), lon = GPSlon();
        modbus.setInput(RS485_INP_GPS_LAT_DEG, (uint16_t)(int)lat);
        modbus.setInput(RS485_INP_GPS_LAT_DEC, (uint16_t)((lat - (int)lat) * 10000.0f));
        modbus.setInput(RS485_INP_GPS_LON_DEG, (uint16_t)(int)lon);
        modbus.setInput(RS485_INP_GPS_LON_DEC, (uint16_t)((lon - (int)lon) * 10000.0f));
        modbus.setInput(RS485_INP_GPS_SPEED,   (uint16_t)(GPSspeed() * 10.0f));
    }

    /* ── Discrete inputs ─────────────────────────────────────────────── */
    modbus.setDI(RS485_DI_WIFI_OK,  GetWiFiStatus() == 1);
    modbus.setDI(RS485_DI_MQTT_OK,  GetMQTTStatus() == 1);
    modbus.setDI(RS485_DI_CELL_OK,  cellStat == 1 || cellStat == 5);
    modbus.setDI(RS485_DI_GPS_FIX,  GPShasFix());
    modbus.setDI(RS485_DI_LOW_BATT, GetCellAlert());

    /* ── Snapshot writable state before processing ───────────────────── */
    bool     prevCellEn  = modbus.getCoil(RS485_COIL_CELL_ENABLE);
    bool     prevGpsEn   = modbus.getCoil(RS485_COIL_GPS_ENABLE);
    uint16_t prevFlags   = modbus.getHolding(RS485_HOLD_FLAGS);

    /* ── Process one Modbus frame ─────────────────────────────────────── */
    modbus.update();

    /* ── Detect writes from master and log descriptive events ────────── */
    bool newCellEn = modbus.getCoil(RS485_COIL_CELL_ENABLE);
    bool newGpsEn  = modbus.getCoil(RS485_COIL_GPS_ENABLE);
    uint16_t newFlags = modbus.getHolding(RS485_HOLD_FLAGS);

    if (newCellEn != prevCellEn)
        pushEvent(String("Cell enable: ") + (newCellEn ? "ON" : "OFF"));
    if (newGpsEn != prevGpsEn)
        pushEvent(String("GPS enable: ") + (newGpsEn ? "ON" : "OFF"));
    if (newFlags != prevFlags)
        pushEvent(String("Flags written: 0x") + String(newFlags, HEX) +
                  (newFlags & 0x01 ? " (serial debug ON)" : " (serial debug OFF)"));

    /* ── Apply control flags ─────────────────────────────────────────── */
    SetSerialLog(newFlags & 0x01);
}

unsigned int RS485getMsgCount() { return _msgCount; }

String RS485getEvents() {
    /* Return events oldest-first as a newline-delimited string */
    String out;
    uint8_t start = (_evCount < RS485_EVT_MAX) ? 0 : _evHead;
    for (uint8_t i = 0; i < _evCount; i++) {
        out += _events[(start + i) % RS485_EVT_MAX];
        out += '\n';
    }
    return out;
}

uint8_t RS485slaveId()    { return modbus.slaveId(); }
bool    RS485cellEnabled() { return modbus.getCoil(RS485_COIL_CELL_ENABLE); }
bool    RS485gpsEnabled()  { return modbus.getCoil(RS485_COIL_GPS_ENABLE); }
