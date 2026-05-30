/**
 * @file cellular.cpp
 * @brief SIM7070G LTE cellular stack — state machine, AT command layer, and job queue.
 *
 * Architecture overview:
 *  - cellTask_full() runs pinned to Core 0 and drives the CS_* state machine.
 *  - All AT I/O goes through four helpers (atFlush, atCmd, atCapture, atWaitURC)
 *    that operate directly on Serial1 (115200/230400 baud, RX=GPIO17, TX=GPIO16).
 *  - Jobs (Pushover HTTPS, SMS) are queued from any task via Pushover() and
 *    SendTextMsg(); the cellular task drains the queue in CS_CONNECTED / CS_IDLE.
 *  - GPS is polled every 10 s while enabled; the modem stays on continuously
 *    while GPS is active.
 *  - The FONA library is used only for begin(), powerOn(), setNetworkSettings(),
 *    and getSIMCCID(); all data-path commands go directly to Serial1.
 *
 * Hard-won lessons encoded here (see CLAUDE.md for details):
 *  - Modem clock is 1980 on boot — TLS fails until clockInit() runs.
 *  - AT+SHSSL=1 errors without the second argument: must be AT+SHSSL=1,"".
 *  - AT+SHDISC before AT+SHREAD causes "operation not allowed".
 *  - AT+CREG? is GSM-only — use AT+CEREG? for LTE registration status.
 *  - CSQ rssi=99 means "not detectable"; do not attempt bearer with rssi=99.
 */

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

#define RXD2 17
#define TXD2 16

/* Set to 1 to log every AT command sent and every raw byte received. */
#define CELL_AT_VERBOSE 0

static Adafruit_FONA_LTE fona;

/* ── State machine ─────────────────────────────────────────────────────── */

/** @brief Cellular task state identifiers. */
enum CellState : uint8_t {
  CS_BOOT,        /**< Power-on or RST cycle */
  CS_INIT,        /**< fona.begin() + LTE-mode configuration */
  CS_SET_APN,     /**< Set APN, read CCID, start operator search */
  CS_WAIT_REG,    /**< Poll CEREG until registered */
  CS_ENABLE_GPRS, /**< clockInit() + bearerActivate() */
  CS_CHECK_SIM,   /**< Log modem diagnostics */
  CS_CONNECTING,  /**< Verify bearer is active */
  CS_CONNECTED,   /**< Drain initial queue + send startup heartbeat */
  CS_IDLE,        /**< Always-on: drain queue, heartbeat timer, GPS polling */
  CS_POWER_DOWN,  /**< Graceful modem shutdown (duty-cycle mode) */
  CS_SLEEP_WAIT,  /**< Sleep between duty-cycle wakes */
  CS_WATCHDOG,    /**< Watchdog-triggered recovery (falls through to CS_ERROR) */
  CS_ERROR        /**< Error recovery with PWRKEY hard-reset at attempt 3 */
};

/* ── Job queue ─────────────────────────────────────────────────────────── */

#define JOB_PUSHOVER  1  /**< Pushover HTTPS notification job */
#define JOB_SMS       2  /**< SMS message job */
#define JOB_QUEUE_LEN 8  /**< Maximum pending jobs */

/** @brief Cellular job descriptor. */
struct CellJob {
  uint8_t type;
  char    title[48];    /**< Pushover: title string | SMS: destination number */
  char    message[128]; /**< Pushover: body string  | SMS: message text (≤160 chars) */
};

static QueueHandle_t     _jobQueue;
static SemaphoreHandle_t _mutex;

/* ── Shared state (volatile where read from other tasks) ───────────────── */

static bool             _radioRestarted = false;
static bool             _firstConnect   = true;
static volatile bool    _wakeRequested  = false;
static char             _deviceName[24] = "LTE-Device";
static volatile bool    _lteOn          = false;
static volatile uint32_t _nextHbSecs    = 0;
static volatile bool    _lteConnected   = false;
static volatile int8_t  _rssi           = 0;
static volatile uint8_t _lteStatus      = 0;
static char             _statusStr[24]  = "Offline";
static char             _simCCID[21]    = "\xe2\x80\x94";  /* em-dash until read */

static char _lastPovrTitle[48]  = "\xe2\x80\x94";
static bool _lastPovrOk         = false;
static char _lastPovrStatus[40] = "No result yet";

/* ── GPS ───────────────────────────────────────────────────────────────── */

static volatile bool  _gpsEnabled       = false;
static volatile bool  _gpsEnableRequest = false;
static volatile bool  _gpsEnableValue   = false;
static volatile bool  _gpsFix           = false;
static volatile float _gpsLat           = 0.0f;
static volatile float _gpsLon           = 0.0f;
static volatile float _gpsSpeed         = 0.0f;
static volatile float _gpsHeading       = 0.0f;
static volatile float _gpsAlt           = 0.0f;

/* ── Helpers ───────────────────────────────────────────────────────────── */

/**
 * @brief Sanitise a value string for use in AT+SHPARA.
 *
 * Only encodes the three characters that would break the AT command line
 * or the quoted-string delimiter: LF → %0A, CR → %0D, " → %22.
 *
 * @param s Raw parameter value.
 * @return URL-partially-encoded string safe for AT+SHPARA.
 */
static String shParamSanitize(const char* s) {
  String out;
  out.reserve(strlen(s) + 16);
  for (; *s; s++) {
    if      (*s == '\n') out += "%0A";
    else if (*s == '\r') out += "%0D";
    else if (*s == '"')  out += "%22";
    else                 out += *s;
  }
  return out;
}

/**
 * @brief Update the operational status string (mutex-protected).
 * @param s New status string.
 */
static void setStatus(const char* s) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(_statusStr, s, sizeof(_statusStr));
  xSemaphoreGive(_mutex);
}

/**
 * @brief Map a raw AT+CSQ rssi value to dBm and update _rssi.
 *
 * Mapping per SIM7070 AT manual §4.2.65:
 *   0 → -115, 1 → -111, 2–30 → linear -110 to -54, 31 → -52, 99 → leave unchanged.
 *
 * @param n Raw AT+CSQ rssi value.
 */
static void updateRSSI(uint8_t n) {
  if      (n == 0)             _rssi = -115;
  else if (n == 1)             _rssi = -111;
  else if (n == 31)            _rssi = -52;
  else if (n >= 2 && n <= 30) _rssi = (int8_t)map(n, 2, 30, -110, -54);
  /* n==99 (not detectable): leave _rssi at its last known value */
}

/**
 * @brief Query AT+CEREG? and return the EPS registration status value.
 *
 * Bypasses the FONA library to avoid its sendParseReply mutex.
 * Returns 0 on parse failure.
 *
 * @return EPS stat: 1 = home, 5 = roaming, 0/2/3/4 = not registered.
 */
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

/* ── Raw AT command infrastructure ────────────────────────────────────── */

#define AT_RX_SIZE 512
static char _atRxBuf[AT_RX_SIZE];

/** @brief Discard all pending Serial1 bytes and wait 20 ms for the line to settle. */
static void atFlush() {
  while (Serial1.available()) Serial1.read();
  vTaskDelay(pdMS_TO_TICKS(20));
}

/**
 * @brief Send an AT command and wait for an expected response substring.
 *
 * Flushes Serial1 before sending.  Returns @c true as soon as @p expect is
 * found in the accumulated response.  Returns @c false on ERROR or timeout.
 *
 * @param cmd       AT command string (without \\r\\n).
 * @param expect    Substring to wait for, or @c nullptr to wait for timeout.
 * @param timeoutMs Maximum wait time in milliseconds.
 * @return @c true if @p expect was found; @c false on ERROR or timeout.
 */
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
    if (expect && strstr(_atRxBuf, expect)) {
#if CELL_AT_VERBOSE
      Log(LOG, "[%s] << %s\n", cmd, _atRxBuf);
#endif
      return true;
    }
    if (strstr(_atRxBuf, "ERROR")) {
#if CELL_AT_VERBOSE
      Log(LOG, "[%s] << %s\n", cmd, _atRxBuf);
#endif
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
#if CELL_AT_VERBOSE
  Log(LOG, "[%s] << TIMEOUT (%s)\n", cmd, _atRxBuf);
#endif
  return false;
}

/**
 * @brief Send an AT command and fill _atRxBuf until OK/ERROR or timeout.
 *
 * Flushes Serial1 before sending.  _atRxBuf is populated with the full
 * response including the command echo and OK/ERROR terminator.
 *
 * @param cmd       AT command string (without \\r\\n).
 * @param timeoutMs Maximum wait time in milliseconds.
 * @return @c true if any bytes were received within the timeout.
 */
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
    if (strstr(_atRxBuf, "OK") || strstr(_atRxBuf, "ERROR")) {
#if CELL_AT_VERBOSE
      Log(LOG, "[%s] << %s\n", cmd, _atRxBuf);
#endif
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
#if CELL_AT_VERBOSE
  if (n > 0) Log(LOG, "[%s] << TIMEOUT (%s)\n", cmd, _atRxBuf);
#endif
  return n > 0;
}

/**
 * @brief Wait for an unsolicited result code (URC) without sending a command.
 *
 * Fills _atRxBuf.  Returns @c true when @p urc is found, @c false when
 * @p fail is found or the timeout expires.
 *
 * @param urc       URC prefix to wait for (may be @c nullptr to skip success check).
 * @param fail      Failure string to watch for (may be @c nullptr).
 * @param timeoutMs Maximum wait time in milliseconds.
 * @return @c true if @p urc was received before @p fail or timeout.
 */
static bool atWaitURC(const char* urc, const char* fail, uint32_t timeoutMs) {
  _atRxBuf[0] = '\0';
  size_t n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial1.available() && n < AT_RX_SIZE - 1)
      _atRxBuf[n++] = (char)Serial1.read();
    _atRxBuf[n] = '\0';
    if (urc  && strstr(_atRxBuf, urc))  {
#if CELL_AT_VERBOSE
      Log(LOG, "[URC wait=%s] << %s\n", urc, _atRxBuf);
#endif
      return true;
    }
    if (fail && strstr(_atRxBuf, fail)) {
#if CELL_AT_VERBOSE
      Log(LOG, "[URC wait=%s] << FAIL: %s\n", urc, _atRxBuf);
#endif
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
#if CELL_AT_VERBOSE
  Log(LOG, "[URC wait=%s] << TIMEOUT (%s)\n", urc, _atRxBuf);
#endif
  return false;
}

/**
 * @brief Send AT+CSQ, update _rssi, and return the raw rssi value.
 * @return Raw AT+CSQ rssi (0–31 or 99 for "not detectable").
 */
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

/** @brief Log current CSQ/RSSI and CEREG state at LOG level. */
static void logModemDiag() {
  uint8_t csq = readCSQ();
  Log(LOG, "  CSQ raw=%d  RSSI=%d dBm\n", csq, (int)_rssi);
  atCapture("AT+CEREG?", 2000);
  atCapture("AT+COPS?",  3000);
}

/* ── Clock management ──────────────────────────────────────────────────── */

/**
 * @brief Check whether the modem RTC holds a plausible date (year ≥ 2024).
 *
 * The SIM7070G powers up with its clock at 1980-01-06 (GPS epoch 0).
 * TLS certificate expiry checks fail until a valid time is set.
 *
 * @return @c true if the modem year is 2024 or later.
 */
static bool clockIsValid() {
  atCapture("AT+CCLK?", 2000);
  const char* p = strstr(_atRxBuf, "+CCLK:");
  if (!p) return false;
  p += 6;
  while (*p == ' ' || *p == '"') p++;
  return atoi(p) >= 24;
}

/**
 * @brief Set the modem RTC from the firmware build date.
 *
 * Used as a fallback when network time (AT+CTZU) is unavailable.
 * Sets time to 00:00:00 UTC on the build date, which is good enough for
 * TLS certificate validation in most deployments.
 */
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

/**
 * @brief Enable network time sync (AT+CTZU) and poll until the clock is valid.
 *
 * Polls up to 20 × 500 ms = 10 s for the network to provide time.
 * Falls back to clockSetFromBuildDate() if the network does not respond.
 * Must be called before any HTTPS operation.
 */
static void clockInit() {
  atCmd("AT+CTZU=1", "OK", 2000);
  for (int i = 0; i < 20; i++) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (clockIsValid()) {
      Log(NOTIFY, "Cell: clock synced from network\n");
      return;
    }
  }
  if (!clockIsValid()) {
    Log(NOTIFY, "Cell: no network time -- using build date\n");
    clockSetFromBuildDate();
  }
}

/* ── Bearer management ─────────────────────────────────────────────────── */

/**
 * @brief Check whether PDP context 0 is active.
 * @return @c true if AT+CNACT? reports @c "0,1," (active).
 */
static bool bearerIsActive() {
  atCapture("AT+CNACT?", 5000);
  return strstr(_atRxBuf, "0,1,") != nullptr;
}

/**
 * @brief Activate PDP bearer 0 with retry.
 *
 * Sends AT+CNACT=0,1 and waits for the @c "+APP PDP: 0,ACTIVE" URC.
 * Returns immediately if the bearer is already active.
 *
 * @param maxRetries Maximum number of activation attempts.
 * @return @c true if the bearer is active on return.
 */
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

/* ── HTTPS session (SH* command set) ──────────────────────────────────── */

/* Forward declaration needed by shCleanup. */
static void shDrainRead(int dlen);

/**
 * @brief Clean up any lingering HTTPS session before opening a new one.
 *
 * If AT+SHSTATE? reports an open session, drains any unread body (SHDISC
 * fails with "operation not allowed" if body data is pending) then issues
 * AT+SHDISC.
 */
static void shCleanup() {
  atCapture("AT+SHSTATE?", 3000);
  if (strstr(_atRxBuf, "+SHSTATE: 1")) {
    shDrainRead(512);
    atCmd("AT+SHDISC", "OK", 10000);
  }
  vTaskDelay(pdMS_TO_TICKS(200));
}

/**
 * @brief Open a TLS 1.2 HTTPS session to api.pushover.net.
 *
 * Cleans up any stale session first, then configures TLS (no cert verify),
 * sets the target URL, and performs the TLS handshake (AT+SHCONN, up to 30 s).
 *
 * @return @c true if AT+SHCONN responded with OK.
 */
static bool shConnect() {
  shCleanup();
  atCmd("AT+CSSLCFG=\"sslversion\",1,3",                "OK", 3000);
  atCmd("AT+SHSSL=1,\"\"",                               "OK", 3000);  /* empty-string arg is mandatory */
  atCmd("AT+SHCONF=\"URL\",\"https://api.pushover.net\"","OK", 3000);
  atCmd("AT+SHCONF=\"BODYLEN\",1024",                    "OK", 3000);
  atCmd("AT+SHCONF=\"HEADERLEN\",350",                   "OK", 3000);
  return atCmd("AT+SHCONN", "OK", 30000);
}

/**
 * @brief Read and discard @p dlen bytes of HTTPS response body.
 *
 * AT+SHDISC returns "operation not allowed" when unread body data is
 * pending.  This function must be called after every AT+SHREQ before
 * calling shDisconnect().
 *
 * @param dlen Number of bytes to read (from the +SHREQ response length field).
 */
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
    if (strstr(_atRxBuf, "ERROR") ||
        (strstr(_atRxBuf, "+SHREAD:") && strstr(_atRxBuf, "OK"))) {
      vTaskDelay(pdMS_TO_TICKS(200));
      while (Serial1.available() && n < AT_RX_SIZE - 1)
        _atRxBuf[n++] = (char)Serial1.read();
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/**
 * @brief Set request headers/parameters and POST to /1/messages.json.
 *
 * Reads the Pushover token and user key from FSInterface.  Sanitises
 * @p title and @p message before passing them to AT+SHPARA.  Waits for
 * the +SHREQ URC (up to 25 s), then drains the response body.
 *
 * @param title   Notification title.
 * @param message Notification body.
 * @return HTTP status code (200 on success); 0 on modem communication failure.
 */
static int shPost(const char* title, const char* message) {
  char cmd[512];

  atCmd("AT+SHCHEAD", "OK", 3000);
  atCmd("AT+SHAHEAD=\"Content-Type\",\"application/x-www-form-urlencoded\"", "OK", 3000);
  atCmd("AT+SHCPARA", "OK", 3000);

  String token   = GetPushoverToken();
  String userKey = GetPushoverUserKey();

  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"token\",\"%s\"", token.c_str());
  atCmd(cmd, "OK", 3000);
  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"user\",\"%s\"", userKey.c_str());
  atCmd(cmd, "OK", 3000);
  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"title\",\"%s\"", shParamSanitize(title).c_str());
  atCmd(cmd, "OK", 3000);
  snprintf(cmd, sizeof(cmd), "AT+SHPARA=\"message\",\"%s\"", shParamSanitize(message).c_str());
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
  p = strchr(p, ',');
  if (!p) return 0;
  int status = atoi(++p);
  p = strchr(p, ',');
  int dlen = p ? atoi(p + 1) : 0;

  shDrainRead(dlen);
  return status;
}

/**
 * @brief Close the HTTPS session.
 *
 * If AT+SHDISC fails (unread body still pending), calls shCleanup() to
 * drain the body and retry the disconnect.
 */
static void shDisconnect() {
  if (!atCmd("AT+SHDISC", "OK", 10000))
    shCleanup();
  vTaskDelay(pdMS_TO_TICKS(200));
}

/* ── SMS via AT+CMGS ───────────────────────────────────────────────────── */

/**
 * @brief Send a single SMS using text mode (AT+CMGF=1).
 *
 * Sequence: CMGF=1 → CMGS="<number>" → wait for '>' → send text + 0x1A.
 * Maximum response time is 60 s per the AT command specification.
 *
 * @param number  Destination phone number string.
 * @param message SMS body text (≤160 chars for a single-part message).
 * @return @c true if +CMGS confirmation was received.
 */
static bool smsSend(const char* number, const char* message) {
  if (!atCmd("AT+CMGF=1", "OK", 3000)) return false;

  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
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
    if (strchr(_atRxBuf, '>'))     break;
    if (strstr(_atRxBuf, "ERROR")) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!strchr(_atRxBuf, '>')) return false;

  Serial1.print(message);
  Serial1.write(0x1A);  /* Ctrl-Z submits the message */

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
  return false;
}

/**
 * @brief Send an SMS with up to 2 attempts and log the outcome.
 * @param number  Destination phone number.
 * @param message SMS body text.
 */
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

/* ── GNSS ──────────────────────────────────────────────────────────────── */

/** @brief Power on the GNSS receiver (AT+CGNSPWR=1). */
static void gpsStart() {
  Log(NOTIFY, "GPS: power on\n");
  bool ok = atCmd("AT+CGNSPWR=1", "OK", 5000);
  _gpsEnabled = ok;
  if (!ok) Log(ERROR, "GPS: power on failed\n");
}

/** @brief Power off the GNSS receiver (AT+CGNSPWR=0) and clear fix flags. */
static void gpsStop() {
  Log(NOTIFY, "GPS: power off\n");
  atCmd("AT+CGNSPWR=0", "OK", 5000);
  _gpsEnabled = false;
  _gpsFix     = false;
}

/**
 * @brief Poll AT+CGNSINF and update GPS position globals.
 *
 * +CGNSINF response fields (Table 8-1 of AT manual):
 *   1=run status, 2=fix status, 3=UTC, 4=lat, 5=lon, 6=alt(m), 7=speed(km/h), 8=course.
 * Position globals (_gpsLat etc.) are only updated when fix status == 1.
 */
static void gpsPoll() {
  if (!atCapture("AT+CGNSINF", 3000)) return;
  const char* p = strstr(_atRxBuf, "+CGNSINF:");
  if (!p) return;
  p += 9;
  while (*p == ' ') p++;
  p = strchr(p, ','); if (!p) return; p++;   /* skip field 1 (run status) */
  int fixStatus = atoi(p);
  p = strchr(p, ','); if (!p) return; p++;   /* skip fix status field */
  p = strchr(p, ','); if (!p) return; p++;   /* skip UTC time field */
  float lat = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float lon = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float alt = atof(p);
  p = strchr(p, ','); if (!p) return; p++;
  float spd = atof(p);
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

/* ── Pushover via HTTPS ────────────────────────────────────────────────── */

/**
 * @brief Send a Pushover notification over HTTPS with up to 3 attempts.
 *
 * Skips immediately if Pushover is disabled in config.  On HTTP 4xx the
 * function does not retry (auth/client errors won't self-heal).  On
 * bearer loss between attempts, attempts reactivation.  Updates
 * _lastPovrTitle, _lastPovrOk, and _lastPovrStatus on completion.
 *
 * @param title   Notification title.
 * @param message Notification body.
 */
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
      if (!bearerIsActive()) {
        Log(NOTIFY, "POVR: bearer lost -- reactivating\n");
        bearerActivate(2);
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
    }
    if (!bearerIsActive()) { Log(ERROR, "POVR: bearer not active\n"); continue; }
    if (!shConnect())      { Log(ERROR, "POVR: HTTPS connect failed (attempt %d/3)\n", attempt + 1); continue; }

    status = shPost(title, message);
    shDisconnect();

    if (status == 200) break;
    if (status >= 400 && status < 500) {
      Log(ERROR, "POVR: HTTP %d -- no retry\n", status);
      break;
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

/**
 * @brief Build and send a status Pushover notification.
 *
 * Message format: "<deviceName> Batt: XX.X% @ X.XXV RSSI: -XX dBm".
 *
 * @param isStartup @c true for the boot notification; @c false for heartbeat.
 */
static void sendStatusNotify(bool isStartup) {
  char msg[160];
  if (_rssi != 0) {
    snprintf(msg, sizeof(msg),
      "%s Batt: %.1f%% @ %.2fV RSSI: %d dBm",
      _deviceName, GetCellSoC(), GetCellV(), (int)_rssi);
  } else {
    snprintf(msg, sizeof(msg),
      "%s Batt: %.1f%% @ %.2fV RSSI: N/A",
      _deviceName, GetCellSoC(), GetCellV());
  }
  doHTTPPushover(isStartup ? "LTE Online - Boot" : "LTE Heartbeat", msg);
}

/* ── Cellular task (Core 0) ────────────────────────────────────────────── */

static volatile CellState _cellState = CS_BOOT;

/**
 * @brief Return a human-readable label for a state machine state.
 * @param s State enum value.
 * @return String literal describing the state.
 */
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

/**
 * @brief Main cellular task body — state machine loop.
 *
 * Runs forever on Core 0.  State transitions:
 *   BOOT → INIT → SET_APN → WAIT_REG → ENABLE_GPRS → CHECK_SIM → CONNECTING → CONNECTED → IDLE
 *   Any failure → ERROR → (retry) BOOT or (after 6 failures) SLEEP_WAIT
 *
 * @param pv Unused FreeRTOS task parameter.
 */
static void cellTask_full(void* pv) {
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
        if (!atCmd("AT", "OK", 3000)) {
          /* RST cycle: safe whether modem is on-but-hung or off */
          Log(NOTIFY, "Cell: no response -- RST cycle\n");
          digitalWrite(FONA_RST, LOW);
          vTaskDelay(pdMS_TO_TICKS(500));
          digitalWrite(FONA_RST, HIGH);
          vTaskDelay(pdMS_TO_TICKS(8000));
          if (!atCmd("AT", "OK", 3000)) {
            Log(NOTIFY, "Cell: RST no response -- PWRKEY power on\n");
            fona.powerOn(FONA_PWRKEY);
            vTaskDelay(pdMS_TO_TICKS(8000));
          } else {
            Log(NOTIFY, "Cell: modem up after RST\n");
            _lteOn = true; _radioRestarted = true;
          }
        } else {
          Log(NOTIFY, "Cell: modem already on\n");
          _lteOn = true; _radioRestarted = true;
        }
        state = CS_INIT;
        break;

      case CS_INIT:
        /* Baud negotiation: if stored AT+IPR=230400 was never applied yet,
           modem may still be at 115200 after a PWRKEY cold start. */
        if (!atCmd("AT", "OK", 500)) {
          Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2);
          if (atCmd("AT", "OK", 1000)) {
            Log(NOTIFY, "Cell: modem at 115200 -- upgrading to 230400\n");
            atCmd("AT+IPR=230400", "OK", 2000);
            vTaskDelay(pdMS_TO_TICKS(50));
          }
          Serial1.begin(230400, SERIAL_8N1, RXD2, TXD2);
        }
        if (fona.begin(Serial1)) {
          Log(NOTIFY, "Cell: modem OK\n");
          _lteOn = true;
          if (!_radioRestarted) {
            /* First init: persist LTE-only mode and apply via CFUN=1,1 RST */
            fona.sendCheckReply(F("AT+CNMP=38"), F("OK"), 2000);
            fona.sendCheckReply(F("AT+CMNB=3"),  F("OK"), 2000);
            Log(NOTIFY, "Cell: LTE mode saved -- RST cycle\n");
            _radioRestarted = true; _lteOn = false;
            while (Serial1.available()) Serial1.read();
            digitalWrite(FONA_RST, LOW);
            vTaskDelay(pdMS_TO_TICKS(1000));
            digitalWrite(FONA_RST, HIGH);
            vTaskDelay(pdMS_TO_TICKS(6000));
            state = CS_INIT;
          } else {
            Log(NOTIFY, "Cell: LTE Cat-M1+NB-IoT active\n");
            atCmd("AT+CMEE=2", "OK", 2000);
            retries = 0; state = CS_SET_APN;
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
        atCapture("AT+CPIN?", 5000);
        atCapture("AT+CFUN?", 2000);
        {
          uint8_t eps = getEPSReg();
          if (eps != 1 && eps != 5) {
            /* Only trigger auto-scan if not already registered;
               COPS=0 while registered causes a deregister+rescan cycle. */
            atCmd("AT+COPS=0", "OK", 10000);
            Log(NOTIFY, "Cell: operator search started\n");
          } else {
            Log(NOTIFY, "Cell: already registered (CEREG=%d) -- skip COPS=0\n", eps);
          }
        }
        retries = 0; state = CS_WAIT_REG;
        break;
      }

      case CS_WAIT_REG: {
        uint8_t eps = getEPSReg();
        uint8_t csq = readCSQ();
        if (retries % 10 == 0) {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d  RSSI=%s  (attempt %d/45)\n",
              eps, csq, csq == 99 ? "no signal" : String((int)_rssi).c_str(), retries + 1);
          atCapture("AT+CFUN?", 2000);
          atCapture("AT+CPIN?", 2000);
          atCapture("AT+COPS?", 3000);
        } else {
          Log(NOTIFY, "Cell: CEREG=%d  CSQ=%d\n", eps, csq);
        }
        if ((eps == 1 || eps == 5) && csq != 99) {
          Log(NOTIFY, "Cell: LTE registered (%s)\n", eps == 5 ? "roaming" : "home");
          retries = 0; state = CS_ENABLE_GPRS;
        } else if (++retries > 45) {
          Log(ERROR, "Cell: registration timed out -- check antenna\n");
          setStatus("No signal"); state = CS_ERROR;
        } else {
          setStatus("Searching...");
          vTaskDelay(pdMS_TO_TICKS(2000));
        }
        break;
      }

      case CS_ENABLE_GPRS: {
        Log(NOTIFY, "Cell: initialising clock and data bearer\n");
        clockInit();
        bool bearerOk = bearerActivate(5);
        Log(bearerOk ? NOTIFY : ERROR, "Cell: bearer %s\n",
            bearerOk ? "active" : "FAILED");
        vTaskDelay(pdMS_TO_TICKS(1000));
        state = CS_CHECK_SIM;
        break;
      }

      case CS_CHECK_SIM:
        logModemDiag();
        retries = 0; state = CS_CONNECTING;
        break;

      case CS_CONNECTING: {
        if (bearerIsActive()) {
          _lteConnected = true;
          setStatus("Connected");
          Log(NOTIFY, "Cell: data bearer confirmed\n");
          watchdogAt = xTaskGetTickCount();
          retries = 0; state = CS_CONNECTED;
        } else {
          Log(ERROR, "Cell: bearer not active -- retrying\n");
          if (++retries > 3) {
            _lteConnected = false; setStatus("No network"); state = CS_ERROR;
          } else {
            bearerActivate(2); vTaskDelay(pdMS_TO_TICKS(3000));
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
        retries = 0; state = CS_IDLE;
        break;
      }

      case CS_IDLE: {
        /* Apply any pending GPS enable/disable request */
        if (_gpsEnableRequest) {
          _gpsEnableRequest = false;
          if (_gpsEnableValue) gpsStart();
          else                 gpsStop();
        }

        /* Drain any jobs queued since CS_CONNECTED */
        {
          CellJob job;
          while (xQueueReceive(_jobQueue, &job, 0) == pdTRUE) {
            if (job.type == JOB_PUSHOVER) doHTTPPushover(job.title, job.message);
            if (job.type == JOB_SMS)      doSMSSend(job.title, job.message);
          }
        }

        if (!_gpsEnabled) {
          readCSQ();
          state = CS_POWER_DOWN;
          break;
        }

        /* GPS active: stay awake; poll position, drain jobs, verify bearer */
        {
          unsigned long nextBearerCheck = millis() + 60000UL;
          unsigned long nextGpsPoll     = millis();

          for (;;) {
            if (_gpsEnableRequest) {
              _gpsEnableRequest = false;
              if (_gpsEnableValue) { gpsStart(); }
              else { gpsStop(); state = CS_POWER_DOWN; break; }
            }

            CellJob j;
            while (xQueueReceive(_jobQueue, &j, 0) == pdTRUE) {
              if (j.type == JOB_PUSHOVER) doHTTPPushover(j.title, j.message);
              if (j.type == JOB_SMS)      doSMSSend(j.title, j.message);
            }

            if ((long)(millis() - nextBearerCheck) >= 0) {
              if (!bearerIsActive()) {
                Log(NOTIFY, "Cell: bearer dropped -- reactivating\n");
                bearerActivate(3);
              }
              nextBearerCheck = millis() + 60000UL;
            }

            if ((long)(millis() - nextGpsPoll) >= 0) {
              gpsPoll();
              nextGpsPoll = millis() + 10000UL;
            }

            readCSQ();
            vTaskDelay(pdMS_TO_TICKS(5000));
          }
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
            Log(ERROR, "Cell: no DOWN -- PWRKEY force off\n");
            digitalWrite(FONA_PWRKEY, LOW);
            vTaskDelay(pdMS_TO_TICKS(1200));
            digitalWrite(FONA_PWRKEY, HIGH);
            vTaskDelay(pdMS_TO_TICKS(1000));
          }
        }
        _lteOn = false;
        setStatus("Sleeping");
        Log(NOTIFY, "Cell: modem off -- sleeping %u min\n", GetHeartbeatMins());
        state = CS_SLEEP_WAIT;
        break;
      }

      case CS_SLEEP_WAIT: {
        unsigned long sleepMs = (unsigned long)GetHeartbeatMins() * 60UL * 1000UL;
        Log(NOTIFY, "Cell: sleeping %u min\n", GetHeartbeatMins());
        unsigned long elapsed = 0;
        while (elapsed < sleepMs) {
          _nextHbSecs = (sleepMs - elapsed) / 1000UL;
          if (_wakeRequested) {
            Log(NOTIFY, "Cell: on-demand wake (pending notification)\n");
            break;
          }
          unsigned long chunk = (sleepMs - elapsed < 10000UL)
                                 ? (sleepMs - elapsed) : 10000UL;
          vTaskDelay(pdMS_TO_TICKS(chunk));
          elapsed += chunk;
        }
        _nextHbSecs = 0;
        if (elapsed >= sleepMs)
          Log(NOTIFY, "Cell: scheduled wake after %u min\n", GetHeartbeatMins());
        retries = 0; state = CS_BOOT;
        break;
      }

      case CS_WATCHDOG:  /* fall through */

      case CS_ERROR:
        _lteOn = false; _lteConnected = false;
        _gpsEnabled = false; _gpsFix = false;
        bootFailCount++;
        if (bootFailCount >= 6) {
          Log(ERROR, "Cell: %d boot failures -- sleeping\n", bootFailCount);
          setStatus("Boot failed"); bootFailCount = 0; retries = 0;
          state = CS_SLEEP_WAIT;
        } else if (bootFailCount == 3) {
          Log(ERROR, "Cell: 3 failures -- PWRKEY modem reset (attempt %d/6)\n", bootFailCount);
          setStatus("Modem reset");
          digitalWrite(FONA_PWRKEY, LOW);
          vTaskDelay(pdMS_TO_TICKS(1500));
          digitalWrite(FONA_PWRKEY, HIGH);
          vTaskDelay(pdMS_TO_TICKS(3000));
          retries = 0; state = CS_BOOT;
        } else {
          Log(ERROR, "Cell: error (attempt %d/6) -- retry in 60s\n", bootFailCount);
          setStatus("Error");
          vTaskDelay(pdMS_TO_TICKS(60000));
          retries = 0; state = CS_BOOT;
        }
        break;
    }
  }
}

static void cellTask(void* p) { cellTask_full(p); }

/* ── Public API ────────────────────────────────────────────────────────── */

/** @brief Create queue/mutex, start Serial1, and pin the cellular task to Core 0. */
void CellTaskStart() {
  _mutex    = xSemaphoreCreateMutex();
  _jobQueue = xQueueCreate(JOB_QUEUE_LEN, sizeof(CellJob));
  Serial1.setRxBufferSize(1024);
  Serial1.begin(230400, SERIAL_8N1, RXD2, TXD2);
  extern String GetUniqueName();
  strlcpy(_deviceName, GetUniqueName().c_str(), sizeof(_deviceName));
  xTaskCreatePinnedToCore(cellTask, "cell", 8192, NULL, 1, NULL, 0);
  Log(NOTIFY, "Cell task started on core 0 -- device: %s\n", _deviceName);
}

/** @brief Queue a Pushover job (non-blocking).  Wakes the modem if sleeping. */
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

/** @brief Request GPS receiver enable/disable (applied in CS_IDLE). */
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

String   CellSigString()         { return String((int)_rssi); }
uint32_t CellNextHeartbeatSecs() { return _nextHbSecs; }
String   CellNetworkString()     { return "hologram"; }
String   CellIPString()          { return "N/A"; }

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

/** @brief Print modem on/connected/RSSI/status to the log at LOG level. */
void CellularDisplay() {
  Log(LOG, "LTE: %s | Conn: %d | RSSI: %d dBm | %s\n",
    _lteOn ? "ON" : "OFF", (int)_lteConnected, (int)_rssi, _statusStr);
}

/** @brief Queue an SMS job (non-blocking).  Wakes the modem if sleeping. */
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

/** @brief Retrieve the last Pushover title and outcome (mutex-protected). */
void CellLastPushover(char* titleOut, bool* okOut, char* statusOut) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  strlcpy(titleOut,  _lastPovrTitle,  48);
  *okOut = _lastPovrOk;
  strlcpy(statusOut, _lastPovrStatus, 40);
  xSemaphoreGive(_mutex);
}
