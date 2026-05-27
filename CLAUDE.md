# LTE Project — Claude Context

## What this project is

ESP32-based IoT device (Olimex ESP32-EVB or similar) with a SIM7070G LTE Cat-M1/NB-IoT cellular modem. Runs FreeRTOS. Primary functions: send Pushover push notifications over HTTPS, send SMS, report GPS position. Non-blocking — the cellular stack runs entirely on Core 0 in its own FreeRTOS task.

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32 (dual-core, Core 0 = cellular task, Core 1 = app) |
| Modem | SIM7070G, firmware `1951B17SIM7070` |
| SIM | Hologram (AT&T LTE), APN = `hologram` |
| Serial | `Serial1` at 115200 baud, RX=GPIO17, TX=GPIO16 |
| Modem pins | `FONA_RST`, `FONA_PWRKEY` (defined in `Define.h`) |

The modem library (`lib/SIM7000/Adafruit_FONA_LTE`) is used only for `fona.begin()`, `powerOn()`, `setNetworkSettings()`, `getSIMCCID()`, and the initial LTE-mode AT commands. All data-path AT commands (HTTPS, SMS, GPS) are sent directly over `Serial1`.

## Key files

| File | Purpose |
|------|---------|
| `src/Hardware/cellular.cpp` | Entire cellular stack — state machine, AT commands, all jobs |
| `src/Hardware/cellular.h` | Public API |
| `src/Hardware/Define.h` | Pin defines, Pushover keys, heartbeat config |
| `src/Hardware/Log.h` | `Log(level, fmt, ...)` logging macro |
| `src/Hardware/FuelGauge.h` | `GetCellSoC()`, `GetCellV()` |

## AT command reference

**Manual:** SIM7070_SIM7080_SIM7090 Series AT Command Manual V1.03 (362 pages). Saved locally from a previous session — ask to re-fetch if needed.

### Verified commands (audited against manual)

| Command | Notes |
|---------|-------|
| `AT+CNMP=38` | LTE-only mode (stored in NVRAM, needs CFUN=1,1 to apply) |
| `AT+CMNB=3` | Cat-M1 + NB-IoT |
| `AT+CEREG?` | EPS (LTE) registration: stat 1=home, 5=roaming |
| `AT+CNACT=0,1` | Activate PDP bearer; URC: `+APP PDP: 0,ACTIVE` |
| `AT+CNACT?` | Bearer status: `+CNACT: <pdpidx>,<statusx>,<addr>` — check for `"0,1,"` |
| `AT+CSQ` | Signal: `+CSQ: <rssi>,<ber>` — rssi 0=-115, 1=-111, 2-30=-110 to -54, 31=-52, 99=unknown |
| `AT+CPSI` | Rich LTE info: System Mode, RSRP, RSRQ, RSSI, RSSNR, band, cell ID |
| `AT+CCLK="YY/MM/DD,HH:MM:SS+00"` | Set clock — critical for TLS handshakes |
| `AT+CTZU=1` | Enable automatic network time update |
| `AT+CCID` | Read SIM ICCID (execution command, no args) |
| `AT+COPS=0` | Automatic operator selection |
| `AT+CPIN?` | SIM PIN status (READY = no PIN needed) |
| `AT+CFUN?` | Phone functionality (1=full) |
| `AT+CPOWD=1` | Graceful modem power down; waits for "DOWN" URC |

### HTTPS (SH* command set)

```
AT+CSSLCFG="sslversion",1,3    → TLS 1.2 on SSL context 1
AT+SHSSL=1,""                   → skip cert verify (empty string arg is MANDATORY)
AT+SHCONF="URL","https://api.pushover.net"
AT+SHCONF="BODYLEN",1024
AT+SHCONF="HEADERLEN",350
AT+SHCONN                        → TLS handshake (up to 30 s)
AT+SHCHEAD                       → clear request headers
AT+SHAHEAD="Content-Type","application/x-www-form-urlencoded"
AT+SHCPARA                       → clear body params
AT+SHPARA="key","value"          → set each param
AT+SHREQ="/1/messages.json",3    → POST; URC: +SHREQ: "POST",<status>,<dlen>
AT+SHREAD=0,<dlen>               → read response body (MUST do before SHDISC)
AT+SHDISC                        → close session
AT+SHSTATE?                      → 0=closed, 1=open (use for cleanup check)
```

**Critical:** Always fully read the body with `AT+SHREAD` before calling `AT+SHDISC`. Calling SHDISC with unread body causes "operation not allowed".

### SMS

```
AT+CMGF=1                        → text mode
AT+CMGS="<number>"               → wait for '>' prompt
<message text>                   → then send 0x1A (Ctrl-Z) to submit
Response: +CMGS: <mr>  OK        → success (up to 60 s per spec)
```

### GNSS

```
AT+CGNSPWR=1                     → power on GNSS receiver
AT+CGNSPWR=0                     → power off
AT+CGNSINF                       → poll position (request/response, no URCs)
```

`AT+CGNSINF` response: `+CGNSINF: <run>,<fix>,<utc>,<lat>,<lon>,<alt>,<speed>,<course>,...`
- Field 2: fix status — **0=no fix, 1=fixed**
- Fields 4/5: latitude/longitude (±dd.dddddd / ±ddd.dddddd)
- Field 6: MSL altitude (metres)
- Field 7: speed over ground (**km/h**)
- Field 8: course over ground (degrees)

## cellular.cpp architecture

### AT command infrastructure

All direct Serial1 I/O goes through four helpers:

```cpp
atFlush()                          // flush Serial1 + 20ms delay
atCmd(cmd, expect, timeoutMs)      // send, return true if expect found
atCapture(cmd, timeoutMs)          // send, fill _atRxBuf until OK/ERROR
atWaitURC(urc, fail, timeoutMs)    // wait for URC (no command sent)
```

`_atRxBuf[512]` is the shared receive buffer. **Every `atCapture`/`atCmd` call flushes the buffer first** — GPS URCs arriving between commands are discarded (acceptable; next CGNSINF poll catches up).

### Job queue

```cpp
#define JOB_PUSHOVER  1
#define JOB_SMS       2
// CellJob.title[48]:   Pushover: title  | SMS: destination number
// CellJob.message[128]: Pushover: body  | SMS: message text (≤160 chars)
```

Queue length 8. Non-blocking enqueue from any task. If modem is in sleep/off state, `_wakeRequested = true` triggers a wake.

### State machine (CS_* states)

```
BOOT → INIT → SET_APN → WAIT_REG → ENABLE_GPRS → CHECK_SIM → CONNECTING → CONNECTED → IDLE
                                                                                         ↕ (stays here forever in always-on mode)
ERROR ←────────────────────────────────────────────────────────────────────────────────┘
```

- **CS_INIT**: `fona.begin()` + set LTE-only mode (CNMP=38, CMNB=3) + hardware RST cycle on first boot
- **CS_ENABLE_GPRS**: `clockInit()` (CTZU=1 + 10s poll + build-date fallback) + `bearerActivate(5)`
- **CS_IDLE**: permanent inner loop — drains job queue, sends heartbeat, checks bearer every 60s, polls GPS every 10s, reads CSQ every 5s
- **CS_ERROR**: clears `_lteOn`, `_lteConnected`, `_gpsEnabled`, `_gpsFix`; PWRKEY hard-reset at attempt 3; sleep-wait at attempt 6

### Key decisions / hard-won lessons

1. **Modem clock is 1980 on boot** — TLS handshakes fail until clock is set. `clockInit()` runs before any HTTPS call.
2. **`AT+SHSSL=1` → "operation not allowed"** — must be `AT+SHSSL=1,""` (empty string second arg).
3. **`AT+SHDISC` before SHREAD → "operation not allowed"** — `shDrainRead()` waits for `+SHREAD:` URC + OK before returning.
4. **Serial1 was 9600, modem expects 115200** — now set correctly in `CellTaskStart()`.
5. **`cellTask()` was dead code** — it powered on the modem and then idled forever. Now calls `cellTask_full()`.
6. **FONA library bypassed for data path** — `fona.*` methods still used for init sequence only. All AT+SH*, AT+CMGS, AT+CGNSPWR/INF go direct to Serial1.
7. **`AT+CREG?` is 2G/GSM** — meaningless in LTE-only mode. Use `AT+CEREG?` for LTE registration.
8. **`AT+CSQ` rssi=99** means "not detectable" — CS_WAIT_REG requires `csq != 99` before transitioning, ensuring signal before attempting bearer.

### Signal/RSSI mapping (verified against spec p.65)

```cpp
n==0  → -115 dBm
n==1  → -111 dBm
n 2-30 → map(n, 2, 30, -110, -54) dBm
n==31  → -52 dBm
else   → 0 (unknown)
```

## Public API summary

```cpp
void   CellTaskStart();                                      // call once in setup()
bool   Pushover(const char* title, const char* message);     // non-blocking, queued
bool   SendTextMsg(const char* number, const char* message); // non-blocking, queued
void   GPSenable(bool on);                                   // toggle GNSS receiver
bool   GPShasFix();
float  GPSlat();  float GPSlon();  float GPSspeed();  float GPSheading();  float GPSaltitude();
bool   CellIsOn();  bool CellIsConnected();
int8_t CellSig();                                            // dBm
String CellStateStr();  String CellStatString();  String CellSIMString();
void   CellLastPushover(char* titleOut, bool* okOut, char* statusOut);
```

## Test tooling (C:\Temp\)

Python scripts used to develop and validate the AT command sequences before writing C code:

| Script | Purpose |
|--------|---------|
| `sim7070g_stress100.py` | 100-message Pushover stress test — confirmed 100% success |
| `sim7070g_clock_https.py` | Clock-fix investigation — found 1980 boot clock bug |
| `sim7070g_https_diag2.py` | HTTPS diagnosis — found SHSSL=1,"" fix |

All scripts use COM16 at 115200. Run with `python <script>.py` on Windows.
