
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
  CS_CONNECTED,       /* drain queue + send startup notify   */
  CS_IDLE,            /* modem stays on — drain queue + heartbeat timer */
  CS_POWER_DOWN,      /* graceful modem shutdown (unused in always-on mode) */
  CS_SLEEP_WAIT,      /* duty-cycle sleep (unused in always-on mode)        */
  CS_WATCHDOG,
  CS_ERROR
};

// ── Job queue ─────────────────────────────────────────────────
#define JOB_PUSHOVER  1
#define JOB_SMS       2
#define JOB_QUEUE_LEN 8

struct CellJob {
  uint8_t type;
  char    title[48];    /* Pushover: title | SMS: destination number */
  char    message[128]; /* Pushover: body  | SMS: message text (≤160 chars) */
};

static QueueHandle_t     _jobQueue;
static SemaphoreHandle_t _mutex;

// ── Shared state ──────────────────────────────────────────────
static bool             _radioRestarted = false;
static bool             _firstConnect   = true;
static volatile bool    _wakeRequested  = false;
static char             _deviceName[24] = "LTE-Device";
static volatile bool    _lteOn          = false;
static volatile bool    _lteConnected   = false;
static volatile int8_t  _rssi           = 0;
static volatile uint8_t _lteStatus      = 0;
static char             _statusStr[24]  = "Offline";
static char             _simCCID[21]    = "\xe2\x80\x94";   /* — */

static char _lastPovrTitle[48]  = "\xe2\x80\x94";
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

// ── Helpers ───────────────────────────────────────────────────
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
  else                         _rssi = 0;
}

/* Query EPS registration directly — bypasses FONA's sendParseReply lock. */
static uint8_t getEPSReg() {
  while (Serial1.available()) Serial1.read();
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
  if (comma) p = comma + 1;
  return (uint8_t)atoi(p);
}

// ── Raw AT command infrastructure ─────────────────────────────
// All functions below write/read Serial1 directly, bypassing the FONA
// library's HTTP layer. Safe because the cellular state machine is
// single-threaded on Core 0.

#define AT_RX_SIZE 512
static char _atRxBuf[AT_RX_SIZE];

static void atFlush() {
  while (Serial1.available()) Serial1.read();
  vTaskDelay(pdMS_TO_TICKS(20));
}

/* Send cmd, return true when response contains expect, false on ERROR/timeout. */
static bool atCmd(const char* cmd, const char* expect, uint32_t timeoutMs) {
  atFlush();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (expect && strstr(_atRxBuf, expect)) return true;
    if (strstr(_atRxBuf, "ERROR"))          return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

/* Send cmd and fill _atRxBuf until OK/ERROR or timeout. */
static bool atCapture(const char* cmd, uint32_t timeoutMs) {
  atFlush();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (strstr(_atRxBuf, "OK") || strstr(_atRxBuf, "ERROR")) return true;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return n > 0;
}

/* Wait for an unsolicited result code (no command sent). Fills _atRxBuf. */
static bool atWaitURC(const char* urc, const char* fail, uint32_t timeoutMs) {
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (urc  && strstr(_atRxBuf, urc))  return true;
    if (fail && strstr(_atRxBuf, fail)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

/* Read AT+CSQ, update _rssi, return raw rssi (99 = unknown). */
static uint8_t readCSQ() {
  if (atCapture("AT+CSQ", 2000)) {
    const char* p = strstr(_atRxBuf, "+CSQ:");
    if (p) {
      uint8_t n = (uint8_t)atoi(p + 5);
      updateRSSI(n);
      return n;
    }
  }
  return 99;
}

static void logModemDiag() {
  uint8_t csq = readCSQ();
  Log(LOG, "  CSQ raw=%d  RSSI=%d dBm\n", csq, (int)_rssi);
  if (atCapture("AT+CEREG?", 2000)) Log(LOG, "  %s\n", _atRxBuf);
  if (atCapture("AT+CPSI",   5000)) Log(LOG, "  %s\n", _atRxBuf);
}

// ── Clock management ──────────────────────────────────────────
// TLS handshakes require a valid wall-clock time. The SIM7070G boots with
// its clock at 1980-01-06 (epoch 0) — every server cert appears expired,
// causing AT+SHCONN to fail. We sync from the network via AT+CTZU and fall
// back to the firmware build date when the network has no NTP coverage yet.

static bool clockIsValid() {
  atCapture("AT+CCLK?", 2000);
  const char* p = strstr(_atRxBuf, "+CCLK:");
  if (!p) return false;
  p += 6;
  while (*p == ' ' || *p == '"') p++;
  return atoi(p) >= 24;   /* YY >= 24 means 2024 or later */
}

static void clockSetFromBuildDate() {
  static const char* const months[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
  };
  char mon[4] = {};
  int day = 1, year = 2026;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  int m = 1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(mon, months[i], 3) == 0) { m = i + 1; break; }
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+CCLK=\"%02d/%02d/%02d,00:00:00+00\"",
           year % 100, m, day);
  atCmd(cmd, "OK", 3000);
  Log(NOTIFY, "Cell: clock set from build date %s\n", __DATE__);
}

static void clockInit() {
  atCmd("AT+CTZU=1", "OK", 2000);   /* enable network time auto-sync */
  for (int i = 0; i < 20; i++) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (clockIsValid()) {
      Log(NOTIFY, "Cell: clock synced from network\n");
      return;
    }
  }
  if (!clockIsValid()) {
    Log(NOTIFY, "Cell: no network time — using build date\n");
    clockSetFromBuildDate();
  }
}

// ── Bearer management ─────────────────────────────────────────
// SIM7070G data bearer: AT+CNACT=0,1 activates PDP context 0.
// Modem emits "+APP PDP: 0,ACTIVE" URC on success.

static bool bearerIsActive() {
  atCapture("AT+CNACT?", 5000);
  return strstr(_atRxBuf, "0,1,") != nullptr;
}

static bool bearerActivate(uint8_t maxRetries = 3) {
  for (uint8_t i = 0; i < maxRetries; i++) {
    if (bearerIsActive()) return true;
    Log(NOTIFY, "Cell: activating bearer (attempt %d/%d)\n", i + 1, maxRetries);
    atFlush();
    Serial1.print("AT+CNACT=0,1\r\n");
    if (atWaitURC("+APP PDP: 0,ACTIVE", "DEACTIVE", 30000)) {
      Log(NOTIFY, "Cell: bearer active\n");
      return true;
    }
    Log(ERROR, "Cell: bearer activate failed\n");
    if (i + 1 < maxRetries) vTaskDelay(pdMS_TO_TICKS(5000));
  }
  return false;
}

// ── HTTPS session (SH* command set) ───────────────────────────
// Per-notification open/close keeps the session fresh and avoids the
// "pending +SHREAD" stall that blocks SHCHEAD/SHDISC on a stale session.

static void shCleanup() {
  atCapture("AT+SHSTATE?", 3000);
  if (strstr(_atRxBuf, "+SHSTATE: 1")) {
    vTaskDelay(pdMS_TO_TICKS(200));
    while (Serial1.available()) Serial1.read();   /* drain any pending body */
    atCmd("AT+SHDISC", "OK", 10000);
  }
  vTaskDelay(pdMS_TO_TICKS(200));
}

static bool shConnect() {
  shCleanup();
  atCmd("AT+CSSLCFG=\"sslversion\",1,3",           "OK", 3000);  /* TLS 1.2, ctx 1 */
  atCmd("AT+SHSSL=1,\"\"",                           "OK", 3000);  /* skip cert verify */
  atCmd("AT+SHCONF=\"URL\",\"https://api.pushover.net\"", "OK", 3000);
  atCmd("AT+SHCONF=\"BODYLEN\",1024",               "OK", 3000);
  atCmd("AT+SHCONF=\"HEADERLEN\",350",              "OK", 3000);
  return atCmd("AT+SHCONN", "OK", 30000);           /* TLS handshake — up to 30 s */
}

static void shDrainRead(int dlen) {
  if (dlen <= 0) return;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+SHREAD=0,%d", dlen);
  atFlush();
  Serial1.print(cmd);
  Serial1.print("\r\n");
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    /* Wait for +SHREAD: header AND OK/ERROR before declaring done */
    if (strstr(_atRxBuf, "+SHREAD:") &&
        (strstr(_atRxBuf, "OK") || strstr(_atRxBuf, "ERROR"))) {
      vTaskDelay(pdMS_TO_TICKS(200));   /* let trailing bytes settle */
      while (Serial1.available() && n < AT_RX_SIZE - 1)
        _atRxBuf[n++] = (char)Serial1.read();
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* Returns HTTP status code (200 on success, 0 on modem failure). */
static int shPost(const char* title, const char* message) {
  char cmd[512];

  atCmd("AT+SHCHEAD", "OK", 3000);   /* clear request headers */
  atCmd("AT+SHAHEAD=\"Content-Type\",\"application/x-www-form-urlencoded\"", "OK", 3000);
  atCmd("AT+SHCPARA", "OK", 3000);   /* clear body params */

  String token   = GetPushoverToken();
  String userKey = GetPushoverUserKey();

  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"token\",\"%s\"", token.c_str());
  atCmd(cmd, "OK", 3000);

  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"user\",\"%s\"", userKey.c_str());
  atCmd(cmd, "OK", 3000);

  String encTitle = urlencode(String(title));
  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"title\",\"%s\"", encTitle.c_str());
  atCmd(cmd, "OK", 3000);

  String encMsg = urlencode(String(message));
  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"message\",\"%s\"", encMsg.c_str());
  atCmd(cmd, "OK", 3000);

  atFlush();
  Serial1.print("AT+SHREQ=\"/1/messages.json\",3\r\n");
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 25000) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (strstr(_atRxBuf, "+SHREQ:") || strstr(_atRxBuf, "ERROR")) break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  /* Parse +SHREQ: "POST",<status>,<dlen> */
  const char* p = strstr(_atRxBuf, "+SHREQ:");
  if (!p) return 0;
  p = strchr(p, ',');   /* skip "POST" field */
  if (!p) return 0;
  int status = atoi(++p);
  p = strchr(p, ',');
  int dlen = p ? atoi(p + 1) : 0;

  shDrainRead(dlen);   /* must fully read before SHDISC */
  return status;
}

static void shDisconnect() {
  atCmd("AT+SHDISC", "OK", 10000);
  vTaskDelay(pdMS_TO_TICKS(200));
}

// ── SMS via AT+CMGS ───────────────────────────────────────────
// Text-mode send per §4.2.5 of AT Command Manual V1.03.
// Sequence: CMGF=1 → CMGS="<num>" → wait '>' → text + 0x1A → +CMGS:<mr> OK
// Max response time per spec: 60 s.

static bool smsSend(const char* number, const char* message) {
  if (!atCmd("AT+CMGF=1", "OK", 3000)) return false;   /* text mode */

  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
  atFlush();
  Serial1.print(cmd);
  Serial1.print("\r\n");

  /* Wait for '>' prompt */
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (strchr(_atRxBuf, '>'))          break;
    if (strstr(_atRxBuf, "ERROR"))      return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!strchr(_atRxBuf, '>')) return false;

  /* Send message body then Ctrl-Z (0x1A) to submit */
  Serial1.print(message);
  Serial1.write(0x1A);

  /* Wait for +CMGS: <mr> OK — up to 60 s per spec */
  _atRxBuf[0] = '\0';
  n = 0;
  t0 = millis();
  while (millis() - t0 < 60000) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (strstr(_atRxBuf, "+CMGS:")) return true;
    if (strstr(_atRxBuf, "ERROR"))  return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;   /* timeout */
}

static void doSMSSend(const char* number, const char* message) {
  Log(NOTIFY, "SMS: sending to %s\n", number);
  bool ok = false;
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0) {
      Log(NOTIFY, "SMS: retry\n");
      vTaskDelay(pdMS_TO_TICKS(3000));
    }
    ok = smsSend(number, message);
    if (ok) break;
    Log(ERROR, "SMS: attempt %d failed\n", attempt + 1);
  }
  Log(ok ? NOTIFY : ERROR, "SMS: %s to %s\n", ok ? "sent" : "FAILED", number);
}

// ── GNSS ──────────────────────────────────────────────────────
// AT+CGNSPWR=1/0  : power the GNSS receiver on or off.
// AT+CGNSINF      : poll navigation info — simple request/response, no URCs.
//
// +CGNSINF response fields (Table 8-1):
//   1  GNSS run status (1=on)
//   2  Fix status      (0=no fix, 1=fixed)
//   3  UTC date+time   (yyyyMMddhhmmss.sss)
//   4  Latitude        ±dd.dddddd
//   5  Longitude       ±ddd.dddddd
//   6  MSL Altitude    metres
//   7  Speed Over Ground  km/h
//   8  Course Over Ground degrees

static void gpsStart() {
  Log(NOTIFY, "GPS: power on\n");
  bool ok = atCmd("AT+CGNSPWR=1", "OK", 5000);
  _gpsEnabled = ok;
  if (!ok) Log(ERROR, "GPS: power on failed\n");
}

static void gpsStop() {
  Log(NOTIFY, "GPS: power off\n");
  atCmd("AT+CGNSPWR=0", "OK", 5000);
  _gpsEnabled = false;
  _gpsFix     = false;
}

static void gpsPoll() {
  if (!atCapture("AT+CGNSINF", 3000)) return;
  const char* p = strstr(_atRxBuf, "+CGNSINF:");
  if (!p) return;
  p += 9;
  while (*p == ' ') p++;
  p = strchr(p, ','); if (!p) return; p++;   /* skip field 1 (run status) */
  int fixStatus = atoi(p);
  p = strchr(p, ','); if (!p) return; p++;   /* skip field 2 (fix status) */
  p = strchr(p, ','); if (!p) return; p++;   /* skip field 3 (UTC time) */
  float lat = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float lon = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float alt = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float spd = atof(p);                        /* km/h */
  p = strchr(p, ','); if (!p) return; p++;
  float hdg = atof(p);

  bool fix = (fixStatus == 1);
  _gpsFix = fix;
  if (fix) {
    _gpsLat     = lat;
    _gpsLon     = lon;
    _gpsAlt     = alt;
    _gpsSpeed   = spd;
    _gpsHeading = hdg;
  }
  Log(LOG, "GPS: fix=%d lat=%.6f lon=%.6f alt=%.1f spd=%.1f km/h\n",
      fix, fix ? (double)lat : 0.0, fix ? (double)lon : 0.0,
      fix ? (double)alt : 0.0, fix ? (double)spd : 0.0);
}

// ── Pushover via HTTPS ────────────────────────────────────────
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

  Log(NOTIFY, "POVR: sending \"%s\"\n", title);

  int status = 0;
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      Log(NOTIFY, "POVR: retry %d/3\n", attempt + 1);
      shCleanup();
      if (!bearerIsActive()) {
        Log(NOTIFY, "POVR: bearer lost — reactivating\n");
        bearerActivate(2);
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
    }

    if (!bearerIsActive()) {
      Log(ERROR, "POVR: bearer not active\n");
      continue;
    }

    if (!shConnect()) {
      Log(ERROR, "POVR: HTTPS connect failed (attempt %d/3)\n", attempt + 1);
      shCleanup();
      continue;
    }

    status = shPost(title, message);
    shDisconnect();

    if (status == 200) break;
    if (status >= 400 && status < 500) {
      Log(ERROR, "POVR: HTTP %d — no retry\n", status);
      break;   /* auth/client error, retrying won't help */
    }
    if (status > 0)
      Log(ERROR, "POVR: HTTP %d on attempt %d\n", status, attempt + 1);
  }

  bool ok = (status == 200);
  char statusStr[40];
  if (ok)
    strlcpy(statusStr, "OK", sizeof(statusStr));
  else
    snprintf(statusStr, sizeof(statusStr), "FAILED (HTTP %d)", status);

  Log(ok ? NOTIFY : ERROR, "POVR: %s \"%s\"\n", ok ? "OK" : "FAILED", title);

  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(_lastPovrTitle,  title,     sizeof(_lastPovrTitle));
  _lastPovrOk = ok;
  strlcpy(_lastPovrStatus, statusStr, sizeof(_lastPovrStatus));
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
    case CS_BOOT:        return "Booting modem";
    case CS_INIT:        return "Initialising";
    case CS_SET_APN:     return "Setting APN";
    case CS_WAIT_REG:    return "Searching network";
    case CS_ENABLE_GPRS: return "Enabling data";
    case CS_CHECK_SIM:   return "Reading SIM";
    case CS_CONNECTING:  return "Connecting";
    case CS_CONNECTED:   return "Connected";
    case CS_IDLE:        return "Online";
    case CS_POWER_DOWN:  return "Powering down";
    case CS_SLEEP_WAIT:  return "Sleeping";
    case CS_WATCHDOG:    return "Watchdog";
    case CS_ERROR:       return "Error";
    default:             return "Unknown";
  }
}

static void cellTask_full(void*) {
  CellState  state         = CS_BOOT;
  uint8_t    retries       = 0;
  uint8_t    bootFailCount = 0;
  TickType_t watchdogAt    = 0;

  for (;;) {
    _cellState = state;
    switch (state) {

      case CS_BOOT:
        Log(NOTIFY, "Cell: booting modem\n");
        pinMode(FONA_RST,    OUTPUT);
        pinMode(FONA_PWRKEY, OUTPUT);
        digitalWrite(FONA_RST, HIGH);
        fona.powerOn(FONA_PWRKEY);
        vTaskDelay(pdMS_TO_TICKS(4000));
        state = CS_INIT;
        break;

      case CS_INIT:
        if (fona.begin(Serial1)) {
          Log(NOTIFY, "Cell: modem OK\n");
          _lteOn = true;
          if (!_radioRestarted) {
            /* First init: set LTE-only mode then hardware-reset radio to apply.
               AT+CNMP/CMNB are stored in NVRAM but need CFUN=1,1 to take effect. */
            fona.sendCheckReply(F("AT+CNMP=38"), F("OK"), 2000);   /* LTE only      */
            fona.sendCheckReply(F("AT+CMNB=3"),  F("OK"), 2000);   /* Cat-M1+NB-IoT */
            Log(NOTIFY, "Cell: LTE mode saved — RST cycle\n");
            _radioRestarted = true;
            _lteOn = false;
            while (Serial1.available()) Serial1.read();
            digitalWrite(FONA_RST, LOW);
            vTaskDelay(pdMS_TO_TICKS(1000));
            digitalWrite(FONA_RST, HIGH);
            vTaskDelay(pdMS_TO_TICKS(6000));
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
        /* Read CCID early so it shows in the portal even if registration is slow */
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
        Log(NOTIFY, "Cell: setting APN hologram\n");
        fona.setNetworkSettings(F("hologram"));
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (atCapture("AT+CPIN?", 5000))  Log(LOG, "  %s\n", _atRxBuf);
        if (atCapture("AT+CFUN?", 2000))  Log(LOG, "  %s\n", _atRxBuf);
        atCmd("AT+COPS=0", "OK", 10000);   /* automatic operator selection */
        Log(NOTIFY, "Cell: operator search started\n");
        retries = 0;
        state   = CS_WAIT_REG;
        break;
      }

      case CS_WAIT_REG: {
        uint8_t eps = getEPSReg();
        uint8_t csq = readCSQ();
        if (retries % 10 == 0) {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d  RSSI=%s  (attempt %d/45)\n",
              eps, csq, csq == 99 ? "no signal" : String((int)_rssi).c_str(), retries + 1);
          if (atCapture("AT+CFUN?", 2000)) Log(LOG, "  %s\n", _atRxBuf);
          if (atCapture("AT+CPIN?", 2000)) Log(LOG, "  %s\n", _atRxBuf);
          if (atCapture("AT+COPS?", 3000)) Log(LOG, "  %s\n", _atRxBuf);
        } else {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d\n", eps, csq);
        }
        if ((eps == 1 || eps == 5) && csq != 99) {
          Log(NOTIFY, "Cell: LTE registered (%s)\n", eps == 5 ? "roaming" : "home");
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
        Log(NOTIFY, "Cell: initialising clock and data bearer\n");
        clockInit();              /* must happen before any HTTPS call */
        bool bearerOk = bearerActivate(5);
        Log(bearerOk ? NOTIFY : ERROR, "Cell: bearer %s\n",
            bearerOk ? "active" : "FAILED");
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        if (bearerIsActive()) {
          _lteConnected = true;
          setStatus("Connected");
          Log(NOTIFY, "Cell: data bearer confirmed\n");
          watchdogAt = xTaskGetTickCount();
          retries    = 0;
          state      = CS_CONNECTED;
        } else {
          /* Bearer should have been activated in CS_ENABLE_GPRS.
             If it dropped in the short window since, CS_ERROR will retry. */
          Log(ERROR, "Cell: bearer not active — retrying\n");
          if (++retries > 3) {
            _lteConnected = false;
            setStatus("No network");
            state = CS_ERROR;
          } else {
            bearerActivate(2);
            vTaskDelay(pdMS_TO_TICKS(3000));
          }
        }
        break;
      }

      case CS_CONNECTED: {
        bootFailCount = 0;
        CellJob job;
        while (xQueueReceive(_jobQueue, &job, 0) == pdTRUE) {
          if (job.type == JOB_PUSHOVER) doHTTPPushover(job.title, job.message);
          if (job.type == JOB_SMS)      doSMSSend(job.title, job.message);
        }
        sendStatusNotify(_firstConnect);
        _firstConnect = false;
        retries = 0;
        state   = CS_IDLE;
        break;
      }

      case CS_IDLE: {
        unsigned long nextHeartbeat   = millis() +
          (unsigned long)GetHeartbeatMins() * 60UL * 1000UL;
        unsigned long nextBearerCheck = millis() + 60000UL;
        unsigned long nextGpsPoll     = millis();   /* poll immediately on entry */

        for (;;) {
          /* Handle GPSenable() requests from other tasks */
          if (_gpsEnableRequest) {
            _gpsEnableRequest = false;
            if (_gpsEnableValue) gpsStart();
            else                 gpsStop();
          }

          CellJob job;
          while (xQueueReceive(_jobQueue, &job, 0) == pdTRUE) {
            if (job.type == JOB_PUSHOVER) doHTTPPushover(job.title, job.message);
            if (job.type == JOB_SMS)      doSMSSend(job.title, job.message);
          }

          if ((long)(millis() - nextHeartbeat) >= 0) {
            sendStatusNotify(false);
            nextHeartbeat = millis() +
              (unsigned long)GetHeartbeatMins() * 60UL * 1000UL;
          }

          if ((long)(millis() - nextBearerCheck) >= 0) {
            if (!bearerIsActive()) {
              Log(NOTIFY, "Cell: bearer dropped — reactivating\n");
              bearerActivate(3);
            }
            nextBearerCheck = millis() + 60000UL;
          }

          if (_gpsEnabled && (long)(millis() - nextGpsPoll) >= 0) {
            gpsPoll();
            nextGpsPoll = millis() + 10000UL;   /* poll every 10 s */
          }

          readCSQ();
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
        break;
      }

      case CS_POWER_DOWN: {
        _lteConnected  = false;
        _wakeRequested = false;
        CellJob lateJob;
        while (xQueueReceive(_jobQueue, &lateJob, 0) == pdTRUE) {
          if (lateJob.type == JOB_PUSHOVER) doHTTPPushover(lateJob.title, lateJob.message);
          if (lateJob.type == JOB_SMS)      doSMSSend(lateJob.title, lateJob.message);
        }
        Log(NOTIFY, "Cell: powering down\n");
        while (Serial1.available()) Serial1.read();
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
            Log(ERROR, "Cell: no AT confirmation — PWRKEY force off\n");
            digitalWrite(FONA_PWRKEY, LOW);
            vTaskDelay(pdMS_TO_TICKS(2500));
            digitalWrite(FONA_PWRKEY, HIGH);
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        _lteOn = false;
        setStatus("Sleeping");
        Log(NOTIFY, "Cell: modem off — sleeping %u min\n", GetHeartbeatMins());
        state = CS_SLEEP_WAIT;
        break;
      }

      case CS_SLEEP_WAIT: {
        unsigned long sleepMs = (unsigned long)GetHeartbeatMins() * 60UL * 1000UL;
        Log(NOTIFY, "Cell: sleeping %u min\n", GetHeartbeatMins());
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
        break;
      }

      case CS_WATCHDOG:   /* fallthrough */

      case CS_ERROR:
        _lteOn        = false;
        _lteConnected = false;
        _gpsEnabled   = false;   /* modem reset clears GNSS state */
        _gpsFix       = false;
        bootFailCount++;
        if (bootFailCount >= 6) {
          Log(ERROR, "Cell: %d boot failures — sleeping\n", bootFailCount);
          setStatus("Boot failed");
          bootFailCount = 0;
          retries = 0;
          state   = CS_SLEEP_WAIT;
        } else if (bootFailCount == 3) {
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

static void cellTask(void* p) { cellTask_full(p); }

// ── Public API ────────────────────────────────────────────────
void CellTaskStart() {
  _mutex    = xSemaphoreCreateMutex();
  _jobQueue = xQueueCreate(JOB_QUEUE_LEN, sizeof(CellJob));
  Serial1.setRxBufferSize(1024);
  Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2);
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
    _wakeRequested = true;
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

bool SendTextMsg(const char* number, const char* message) {
  CellJob job;
  job.type = JOB_SMS;
  strlcpy(job.title,   number,  sizeof(job.title));
  strlcpy(job.message, message, sizeof(job.message));
  if (xQueueSend(_jobQueue, &job, 0) != pdTRUE) {
    Log(ERROR, "SMS: queue full\n");
    return false;
  }
  if (!_lteConnected) _wakeRequested = true;
  return true;
}

void CellLastPushover(char* titleOut, bool* okOut, char* statusOut) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(titleOut,  _lastPovrTitle,  48);
  *okOut = _lastPovrOk;
  strlcpy(statusOut, _lastPovrStatus, 40);
  xSemaphoreGive(_mutex);
}
