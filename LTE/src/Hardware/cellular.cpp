
#include <SIM7000.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "cellular.h"
#include "Define.h"
#include "Log.h"
#include "FuelGauge.h"
#include "../FileSystem/FSInterface.h"

// ── Hardware ──────────────────────────────────────────────────
#define RXD2 17
#define TXD2 16

static Adafruit_FONA_LTE fona;

// ── State machine ─────────────────────────────────────────────
enum CellState : uint8_t {
  CS_BOOT,
  CS_INIT,
  CS_SET_APN,
  CS_WAIT_REG,        /* wait for EPS registration           */
  CS_ENABLE_GPRS,
  CS_CHECK_SIM,
  CS_CONNECTING,
  CS_CONNECTED,       /* drain queue + send notify           */
  CS_POWER_DOWN,      /* graceful modem shutdown             */
  CS_SLEEP_WAIT,      /* 6-hour duty-cycle sleep             */
  CS_WATCHDOG,
  CS_ERROR
};

/* Sleep duration is read from config at runtime via GetHeartbeatMins() */

// ── Job queue ─────────────────────────────────────────────────
#define JOB_PUSHOVER  1
#define JOB_QUEUE_LEN 8

struct CellJob {
  uint8_t type;
  char    title[48];
  char    message[128];
};

static QueueHandle_t     _jobQueue;
static SemaphoreHandle_t _mutex;

// ── Shared state ──────────────────────────────────────────────
static bool             _radioRestarted = false;
static bool             _firstConnect   = true;   /* startup vs heartbeat */
static volatile bool    _wakeRequested  = false;  /* Pushover arrived while asleep */
static char             _deviceName[24] = "LTE-Device";
static volatile bool    _lteOn          = false;
static volatile bool    _lteConnected   = false;
static volatile int8_t  _rssi           = 0;
static volatile uint8_t _lteStatus      = 0;
static char             _statusStr[24]  = "Offline";
static char             _simCCID[21]    = "—";

/* ── Last Pushover result (mutex-protected, read by web portal) ──────── */
static char _lastPovrTitle[48]  = "—";
static bool _lastPovrOk         = false;
static char _lastPovrStatus[40] = "No result yet";

// ── GPS ───────────────────────────────────────────────────────
static volatile bool  _gpsEnabled       = false;
static volatile bool  _gpsEnableRequest = false;
static volatile bool  _gpsEnableValue   = false;
static volatile bool  _gpsFix           = false;
static volatile float _gpsLat           = 0.0f;
static volatile float _gpsLon           = 0.0f;
static volatile float _gpsSpeed         = 0.0f;
static volatile float _gpsHeading       = 0.0f;
static volatile float _gpsAlt           = 0.0f;

// ── Internal helpers ──────────────────────────────────────────
static String urlencode(const String& str) {
  String out = "";
  for (int i = 0; i < (int)str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') {
      out += '+';
    } else if (isalnum(c)) {
      out += c;
    } else {
      char hi = (c >> 4) & 0xf;
      char lo = c & 0xf;
      out += '%';
      out += (char)(hi > 9 ? hi - 10 + 'A' : hi + '0');
      out += (char)(lo > 9 ? lo - 10 + 'A' : lo + '0');
    }
  }
  return out;
}

static void setStatus(const char* s) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(_statusStr, s, sizeof(_statusStr));
  xSemaphoreGive(_mutex);
}


static void updateRSSI(uint8_t n) {
  if      (n == 0)             _rssi = -115;
  else if (n == 1)             _rssi = -111;
  else if (n == 31)            _rssi = -52;
  else if (n >= 2 && n <= 30) _rssi = (int8_t)map(n, 2, 30, -110, -54);
  else                         _rssi = 0;   /* 99 = not detectable */
}

/* AT+CEREG = EPS (LTE) registration — sendParseReply is protected so we
   query Serial1 directly. Response: "+CEREG: [mode,]stat" */
static uint8_t getEPSReg() {
  while (Serial1.available()) Serial1.read();   /* flush */
  Serial1.println("AT+CEREG?");
  vTaskDelay(pdMS_TO_TICKS(400));
  char buf[64] = {0};
  size_t n = 0;
  while (Serial1.available() && n < sizeof(buf) - 1)
    buf[n++] = (char)Serial1.read();
  char* p = strstr(buf, "+CEREG:");
  if (!p) return 0;
  p += 7;
  while (*p == ' ') p++;
  char* comma = strchr(p, ',');
  if (comma) p = comma + 1;   /* "mode,stat" → skip to stat */
  return (uint8_t)atoi(p);
}

static void logModemDiag() {
  uint8_t n = fona.getRSSI();
  updateRSSI(n);
  Log(LOG, "  CSQ raw=%d  RSSI=%d dBm\n", n, (int)_rssi);
  Log(LOG, "  GPRS state=%d\n", (int)fona.GPRSstate());
  Log(LOG, "  WirelessConn=%d\n", (int)fona.wirelessConnStatus());
  fona.sendCheckReply(F("AT+CREG?"),  F("OK"), 1000);
  fona.sendCheckReply(F("AT+CGREG?"), F("OK"), 1000);
}

static void doHTTPPushover(const char* title, const char* message) {
  if (!GetPushoverEnabled()) {
    Log(NOTIFY, "POVR: disabled\n");
    xSemaphoreTake(_mutex, portMAX_DELAY);
    strlcpy(_lastPovrTitle,  title,     sizeof(_lastPovrTitle));
    _lastPovrOk = false;
    strlcpy(_lastPovrStatus, "Disabled", sizeof(_lastPovrStatus));
    xSemaphoreGive(_mutex);
    return;
  }
  char url[32] = "api.pushover.net";
  char body[300];
  String token   = GetPushoverToken();
  String userKey = GetPushoverUserKey();
  int bodyLen = snprintf(body, sizeof(body),
    "token=%s&user=%s&title=%s&message=%s",
    token.c_str(), userKey.c_str(),
    urlencode(String(title)).c_str(),
    urlencode(String(message)).c_str());

  /* Preserve original ordering: ssl before connect.
     The modem keeps HTTPSSL state and HTTP_connect (AT+HTTPINIT) honours it.
     Do NOT call AT+HTTPTERM — the library manages session lifetime. */
  fona.HTTP_ssl(true);
  fona.HTTP_connect(url);
  Log(NOTIFY, "POVR: sending \"%s\"\n", title);
  vTaskDelay(pdMS_TO_TICKS(500));
  char result = fona.HTTP_POST("/1/messages.json", body, bodyLen, 10000);

  bool ok = (result != 0);
  Log(ok ? NOTIFY : ERROR, "POVR: %s — \"%s\"\n", ok ? "OK" : "FAILED", title);

  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(_lastPovrTitle,  title,     sizeof(_lastPovrTitle));
  _lastPovrOk = ok;
  strlcpy(_lastPovrStatus, ok ? "OK" : "FAILED", sizeof(_lastPovrStatus));
  xSemaphoreGive(_mutex);
}

static void sendStatusNotify(bool isStartup) {
  char msg[160];
  snprintf(msg, sizeof(msg),
    "%s\nBatt: %.1f%% @ %.2fV\nRSSI: %d dBm",
    _deviceName, GetCellSoC(), GetCellV(), (int)_rssi);
  doHTTPPushover(isStartup ? "LTE Online - Boot" : "LTE Heartbeat", msg);
}

// ── Cellular task (Core 0) ────────────────────────────────────
static volatile CellState _cellState = CS_BOOT;

static const char* stateLabel(CellState s) {
  switch (s) {
    case CS_BOOT:       return "Booting modem";
    case CS_INIT:       return "Initialising";
    case CS_SET_APN:    return "Setting APN";
    case CS_WAIT_REG:   return "Searching network";
    case CS_ENABLE_GPRS:return "Enabling GPRS";
    case CS_CHECK_SIM:  return "Reading SIM";
    case CS_CONNECTING: return "Connecting";
    case CS_CONNECTED:  return "Connected";
    case CS_POWER_DOWN: return "Powering down";
    case CS_SLEEP_WAIT: return "Sleeping";
    case CS_WATCHDOG:   return "Watchdog";
    case CS_ERROR:      return "Error";
    default:            return "Unknown";
  }
}

static void cellTask(void*) {
  CellState  state        = CS_BOOT;
  uint8_t    retries      = 0;
  uint8_t    bootFailCount = 0;   /* CS_ERROR hits in a row; reset on CS_CONNECTED */
  TickType_t watchdogAt   = 0;

  for (;;) {
    _cellState = state;
    switch (state) {

      case CS_BOOT:
        Log(NOTIFY, "Cell: booting modem\n");
        pinMode(FONA_RST,    OUTPUT);
        pinMode(FONA_PWRKEY, OUTPUT);
        digitalWrite(FONA_RST, HIGH);      /* ensure RST not asserted */
        /* Modem is fully off after CS_POWER_DOWN — PWRKEY pulse powers it on.
           Do NOT pulse RST here: that resets an ON modem, not relevant when off. */
        fona.powerOn(FONA_PWRKEY);
        vTaskDelay(pdMS_TO_TICKS(4000));   /* wait for modem to boot */
        state = CS_INIT;
        break;

      case CS_INIT:
        if (fona.begin(Serial1)) {
          Log(NOTIFY, "Cell: modem OK\n");
          _lteOn = true;
          if (!_radioRestarted) {
            /* First init: set LTE modes then restart radio to apply them.
               AT+CNMP/CMNB are saved to NVRAM but need CFUN=1,1 to take effect. */
            fona.sendCheckReply(F("AT+CNMP=38"), F("OK"), 2000); /* LTE only      */
            fona.sendCheckReply(F("AT+CMNB=3"),  F("OK"), 2000); /* Cat-M1+NB-IoT */
            Log(NOTIFY, "Cell: modes saved — hardware RST cycle\n");
            _radioRestarted = true;
            _lteOn = false;
            while (Serial1.available()) Serial1.read();   /* flush */
            digitalWrite(FONA_RST, LOW);
            vTaskDelay(pdMS_TO_TICKS(1000));
            digitalWrite(FONA_RST, HIGH);
            vTaskDelay(pdMS_TO_TICKS(6000));   /* wait for modem to boot */
            state = CS_INIT;
          } else {
            Log(NOTIFY, "Cell: LTE Cat-M1+NB-IoT active\n");
            retries = 0;
            state   = CS_SET_APN;
          }
        } else {
          Log(ERROR, "Cell: modem not found (attempt %d/3)\n", retries + 1);
          if (++retries > 3) { state = CS_ERROR; break; }
          digitalWrite(FONA_RST, LOW);
          vTaskDelay(pdMS_TO_TICKS(1000));
          digitalWrite(FONA_RST, HIGH);
          vTaskDelay(pdMS_TO_TICKS(4000));
        }
        break;

      case CS_SET_APN: {
        /* Read CCID early so it's visible even if registration fails */
        {
          char ccid[32] = {0};
          int len = fona.getSIMCCID(ccid);
          if (len > 0 && strncmp(ccid, "ERROR", 5) != 0) {
            ccid[len] = '\0';
            xSemaphoreTake(_mutex, portMAX_DELAY);
            strlcpy(_simCCID, ccid, sizeof(_simCCID));
            xSemaphoreGive(_mutex);
            Log(NOTIFY, "SIM CCID: %s\n", ccid);
          } else {
            Log(ERROR, "SIM CCID: not ready (len=%d val=%s)\n", len, ccid);
          }
        }
        Log(NOTIFY, "Cell: setting APN hologram\n");
        fona.setNetworkSettings(F("hologram"));
        vTaskDelay(pdMS_TO_TICKS(1000));
        /* Check SIM PIN and radio state before searching */
        fona.sendCheckReply(F("AT+CPIN?"),  F("OK"), 2000);
        fona.sendCheckReply(F("AT+CFUN?"),  F("OK"), 2000);
        /* Trigger automatic operator search */
        fona.sendCheckReply(F("AT+COPS=0"), F("OK"), 5000);
        Log(NOTIFY, "Cell: operator search started\n");
        retries = 0;
        state   = CS_WAIT_REG;
        break;
      }

      case CS_WAIT_REG: {
        uint8_t eps = getEPSReg();
        uint8_t csq = fona.getRSSI();
        updateRSSI(csq);
        /* Every 10 attempts dump full diagnostics */
        if (retries % 10 == 0) {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d  RSSI=%s  (attempt %d/45)\n",
              eps, csq, csq == 99 ? "no signal" : String((int)_rssi).c_str(), retries + 1);
          fona.sendCheckReply(F("AT+CFUN?"),  F("OK"), 1000);
          fona.sendCheckReply(F("AT+CPIN?"),  F("OK"), 1000);
          fona.sendCheckReply(F("AT+COPS?"),  F("OK"), 2000);
        } else {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d  RSSI=%s\n",
              eps, csq, csq == 99 ? "no signal" : String((int)_rssi).c_str());
        }
        if ((eps == 1 || eps == 5) && csq != 99) {
          Log(NOTIFY, "Cell: LTE registered (%s)\n",
              eps == 5 ? "roaming" : "home");
          retries = 0;
          state   = CS_ENABLE_GPRS;
        } else if (++retries > 45) {
          Log(ERROR, "Cell: registration timed out — check antenna\n");
          setStatus("No signal");
          state = CS_ERROR;
        } else {
          setStatus("Searching...");
          vTaskDelay(pdMS_TO_TICKS(2000));
        }
        break;
      }

      case CS_ENABLE_GPRS: {
        Log(NOTIFY, "Cell: enabling GPRS\n");
        bool gprsOk = fona.enableGPRS(true);
        Log(NOTIFY, "Cell: enableGPRS=%d\n", (int)gprsOk);
        vTaskDelay(pdMS_TO_TICKS(2000));   /* allow GPRS to register */
        state = CS_CHECK_SIM;
        break;
      }

      case CS_CHECK_SIM: {
        char ccid[32] = {0};
        bool simOk = false;
        for (uint8_t i = 0; i < 5; i++) {
          int len = fona.getSIMCCID(ccid);
          Log(LOG, "Cell: CCID attempt %d len=%d val=%s\n", i + 1, len, ccid);
          if (len > 0 && strncmp(ccid, "ERROR", 5) != 0) {
            ccid[len] = '\0';
            xSemaphoreTake(_mutex, portMAX_DELAY);
            strlcpy(_simCCID, ccid, sizeof(_simCCID));
            xSemaphoreGive(_mutex);
            Log(NOTIFY, "SIM CCID: %s\n", ccid);
            simOk = true;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!simOk) Log(ERROR, "SIM CCID: not ready after 5 attempts\n");
        Log(NOTIFY, "Cell: pre-connect diagnostics\n");
        logModemDiag();
        retries = 0;
        state   = CS_CONNECTING;
        break;
      }

      case CS_CONNECTING: {
        int8_t wcs = fona.wirelessConnStatus();
        Log(NOTIFY, "Cell: wirelessConnStatus=%d\n", wcs);
        if (wcs > 0) {
          _lteConnected = true;
          setStatus("Connected");
          Log(NOTIFY, "Cell: already connected\n");
          watchdogAt = xTaskGetTickCount();
          retries    = 0;
          state      = CS_CONNECTED;
        } else {
          bool opened = fona.openWirelessConnection(true);
          Log(NOTIFY, "Cell: openWirelessConn=%d (retry %d/24)\n", (int)opened, retries + 1);
          if (opened) {
            _lteConnected = true;
            setStatus("Connected");
            Log(NOTIFY, "Cell: data OK\n");
            watchdogAt = xTaskGetTickCount();
            retries    = 0;
            state      = CS_CONNECTED;
          } else {
            if (++retries > 24) {
              Log(ERROR, "Cell: connect failed — retry in 60s\n");
              logModemDiag();
              _lteConnected = false;
              setStatus("No network");
              state = CS_ERROR;
            } else {
              vTaskDelay(pdMS_TO_TICKS(2000));
            }
          }
        }
        break;
      }

      case CS_CONNECTED: {
        bootFailCount = 0;   /* successful connection — reset error counter */
        /* Drain all pending notification jobs before shutting down */
        CellJob job;
        while (xQueueReceive(_jobQueue, &job, 0) == pdTRUE) {
          if (job.type == JOB_PUSHOVER) doHTTPPushover(job.title, job.message);
        }
        /* Send startup / heartbeat status notification */
        sendStatusNotify(_firstConnect);
        _firstConnect = false;
        retries = 0;
        state   = CS_POWER_DOWN;
        break;
      }

      case CS_POWER_DOWN: {
        /* Mark disconnected BEFORE the drain so any concurrent Pushover() on Core 1
           will see _lteConnected==false and correctly set _wakeRequested. */
        _lteConnected  = false;
        _wakeRequested = false;
        /* Drain any jobs that arrived after the CS_CONNECTED drain */
        CellJob lateJob;
        while (xQueueReceive(_jobQueue, &lateJob, 0) == pdTRUE) {
          if (lateJob.type == JOB_PUSHOVER) doHTTPPushover(lateJob.title, lateJob.message);
        }
        Log(NOTIFY, "Cell: powering down\n");
        /* Graceful AT shutdown — wait for "NORMAL POWER DOWN" URC (up to 8 s).
           Do NOT pulse PWRKEY after this: from an OFF state PWRKEY LOW >1s = power ON,
           which would immediately re-boot the modem. */
        while (Serial1.available()) Serial1.read();   /* flush */
        Serial1.println("AT+CPOWD=1");
        {
          char buf[64] = {0};
          size_t n = 0;
          unsigned long t0 = millis();
          while (millis() - t0 < 8000) {
            while (Serial1.available() && n < sizeof(buf) - 1)
              buf[n++] = (char)Serial1.read();
            if (strstr(buf, "DOWN")) {
              Log(NOTIFY, "Cell: AT power down confirmed\n");
              break;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
          }
          if (!strstr(buf, "DOWN")) {
            /* AT command not confirmed — force off with PWRKEY from an ON state */
            Log(ERROR, "Cell: no AT confirmation — PWRKEY force off\n");
            digitalWrite(FONA_PWRKEY, LOW);
            vTaskDelay(pdMS_TO_TICKS(2500));   /* SIM7000: >1.2 s from ON = power off */
            digitalWrite(FONA_PWRKEY, HIGH);
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        _lteOn = false;
        /* _radioRestarted intentionally NOT cleared — LTE mode is stored in modem NVRAM */
        setStatus("Sleeping");
        Log(NOTIFY, "Cell: modem off — sleeping %u min\n", GetHeartbeatMins());
        state = CS_SLEEP_WAIT;
        break;
      }

      case CS_SLEEP_WAIT: {
        unsigned long sleepMs = (unsigned long)GetHeartbeatMins() * 60UL * 1000UL;
        Log(NOTIFY, "Cell: sleeping %u min\n", GetHeartbeatMins());
        /* Sleep in 10 s chunks so an on-demand wake request is noticed quickly */
        unsigned long elapsed = 0;
        while (elapsed < sleepMs) {
          if (_wakeRequested) {
            Log(NOTIFY, "Cell: on-demand wake (pending notification)\n");
            break;
          }
          unsigned long chunk = (sleepMs - elapsed < 10000UL)
                                 ? (sleepMs - elapsed) : 10000UL;
          vTaskDelay(pdMS_TO_TICKS(chunk));
          elapsed += chunk;
        }
        if (elapsed >= sleepMs)
          Log(NOTIFY, "Cell: scheduled wake after %u min\n", GetHeartbeatMins());
        retries = 0;
        state   = CS_BOOT;
        /* _wakeRequested is cleared at the top of CS_POWER_DOWN on the next cycle */
        break;
      }

      case CS_WATCHDOG:   /* fallthrough — not used in duty-cycle mode */

      case CS_ERROR:
        _lteOn        = false;
        _lteConnected = false;
        bootFailCount++;
        if (bootFailCount >= 6) {
          /* 6 consecutive failures — give up, go to sleep */
          Log(ERROR, "Cell: %d boot failures — sleeping\n", bootFailCount);
          setStatus("Boot failed");
          bootFailCount = 0;
          retries = 0;
          state   = CS_SLEEP_WAIT;
        } else if (bootFailCount == 3) {
          /* 3 failures — hardware modem reset before next attempt */
          Log(ERROR, "Cell: 3 failures — PWRKEY modem reset (attempt %d/6)\n", bootFailCount);
          setStatus("Modem reset");
          digitalWrite(FONA_PWRKEY, LOW);
          vTaskDelay(pdMS_TO_TICKS(1500));
          digitalWrite(FONA_PWRKEY, HIGH);
          vTaskDelay(pdMS_TO_TICKS(3000));
          retries = 0;
          state   = CS_BOOT;
        } else {
          Log(ERROR, "Cell: error (attempt %d/6) — retry in 60s\n", bootFailCount);
          setStatus("Error");
          vTaskDelay(pdMS_TO_TICKS(60000));
          retries = 0;
          state   = CS_BOOT;
        }
        break;
    }
  }
}

// ── Public API ────────────────────────────────────────────────
void CellTaskStart() {
  _mutex    = xSemaphoreCreateMutex();
  _jobQueue = xQueueCreate(JOB_QUEUE_LEN, sizeof(CellJob));
  Serial1.setRxBufferSize(1024);
  Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2);
  /* Capture device name now (WiFi MAC is available, called after WiFiBootSequence) */
  extern String GetUniqueName();
  strlcpy(_deviceName, GetUniqueName().c_str(), sizeof(_deviceName));
  xTaskCreatePinnedToCore(cellTask, "cell", 8192, NULL, 1, NULL, 0);
  Log(NOTIFY, "Cell task started on core 0 — device: %s\n", _deviceName);
}

bool Pushover(const char* title, const char* message) {
  CellJob job;
  job.type = JOB_PUSHOVER;
  strlcpy(job.title,   title,   sizeof(job.title));
  strlcpy(job.message, message, sizeof(job.message));
  if (xQueueSend(_jobQueue, &job, 0) != pdTRUE) {
    Log(ERROR, "POVR: queue full\n");
    return false;
  }
  if (!_lteConnected) {
    _wakeRequested = true;   /* modem is sleeping — wake it to send */
    Log(NOTIFY, "POVR: queued, waking modem\n");
  }
  return true;
}

void GPSenable(bool on) {
  _gpsEnableValue   = on;
  _gpsEnableRequest = true;
}

bool   CellIsOn()        { return _lteOn; }
bool   CellIsConnected() { return _lteConnected; }
int8_t CellSig()         { return _rssi; }
String CellStateStr()    { return String(stateLabel(_cellState)); }

String CellStatString() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  String s(_statusStr);
  xSemaphoreGive(_mutex);
  return s;
}

String CellSIMString() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  String s(_simCCID);
  xSemaphoreGive(_mutex);
  return s;
}

String CellSigString()     { return String(_rssi); }
String CellNetworkString() { return "hologram"; }
String CellIPString()      { return "N/A"; }

String CellGPSString() {
  if (!_gpsEnabled) return "OFF";
  return _gpsFix ? "Fix" : "No Fix";
}
String CellLATString() { return _gpsFix ? String(_gpsLat, 6) : "N/A"; }
String CellLONString() { return _gpsFix ? String(_gpsLon, 6) : "N/A"; }

bool   GPSisEnabled() { return _gpsEnabled; }
bool   GPShasFix()    { return _gpsFix; }
float  GPSlat()       { return _gpsLat; }
float  GPSlon()       { return _gpsLon; }
float  GPSspeed()     { return _gpsSpeed; }
float  GPSheading()   { return _gpsHeading; }
float  GPSaltitude()  { return _gpsAlt; }
String GPSlatString() { return _gpsFix ? String(_gpsLat, 6) : "N/A"; }
String GPSlonString() { return _gpsFix ? String(_gpsLon, 6) : "N/A"; }

void CellularDisplay() {
  Log(LOG, "LTE: %s | Conn: %d | RSSI: %d dBm | %s\n",
    _lteOn ? "ON" : "OFF", (int)_lteConnected, (int)_rssi, _statusStr);
}

bool SendTextMsg() { return false; }

void CellLastPushover(char* titleOut, bool* okOut, char* statusOut) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(titleOut,  _lastPovrTitle,  48);
  *okOut = _lastPovrOk;
  strlcpy(statusOut, _lastPovrStatus, 40);
  xSemaphoreGive(_mutex);
}
