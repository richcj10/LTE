#include "WiFi.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "Hardware/Log.h"
#include "Define.h"
#include "FileSystem/FSInterface.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

#define WIFI_CONNECT_MS  30000UL   /* 30 s to connect via NVS creds */
#define AP_TIMEOUT_MS   300000UL   /* 5 min AP window before retry   */

WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "3.north-america.pool.ntp.org", 3600, 60000);

static const char AP_SSID_PREFIX[]  = "LTE-Setup";
static const char AP_PASSWORD[]     = "LTEAP";

static bool _haveWiFi      = false;
static char _status        = 0;
static bool _apConfigSaved = false;
IPAddress   CurrentIP;

static AsyncWebServer apServer(80);
static DNSServer      dnsServer;

// ── Embedded AP config page ───────────────────────────────────
static const char AP_PAGE[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LTE Device Setup</title>
<style>
  body{font-family:sans-serif;max-width:420px;margin:40px auto;padding:0 16px;color:#1e293b}
  h2{color:#2563eb;margin-bottom:4px}
  p{color:#64748b;font-size:.9em;margin-top:0}
  h3{margin:16px 0 4px;font-size:1em}
  label{display:block;margin:8px 0 2px;font-size:.85em;color:#475569}
  input{width:100%;padding:8px;border:1px solid #cbd5e1;border-radius:6px;
        box-sizing:border-box;font-size:.95em}
  hr{margin:20px 0;border:none;border-top:1px solid #e2e8f0}
  button{width:100%;padding:11px;background:#2563eb;color:#fff;border:none;
         border-radius:6px;font-size:1em;cursor:pointer;margin-top:14px}
  button:hover{background:#1d4ed8}
  .opt{color:#94a3b8;font-size:.8em}
</style></head>
<body>
<h2>LTE Device Setup</h2>
<p>Connect to your WiFi network and optionally configure MQTT.</p>
<form action="/save" method="POST">
  <h3>WiFi</h3>
  <label>SSID</label><input name="ssid" required>
  <label>Password</label><input name="wpass" type="password">
  <label>Hostname</label><input name="host" value="LTE-Device">
  <hr>
  <h3>MQTT <span class="opt">(optional)</span></h3>
  <label>Broker IP</label><input name="mip">
  <label>Port</label><input name="mport" value="1883">
  <label>Username</label><input name="muser">
  <label>Password</label><input name="mpass" type="password">
  <button type="submit">Save &amp; Connect</button>
</form>
</body></html>
)html";

static const char AP_SAVED[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>body{font-family:sans-serif;max-width:420px;margin:60px auto;text-align:center;color:#1e293b}</style>
</head><body>
<h2 style="color:#16a34a">&#10003; Config saved</h2>
<p>Device is restarting and will connect to your WiFi network.</p>
<p style="color:#94a3b8;font-size:.85em">You can reconnect to your normal network.</p>
</body></html>
)html";

// ── AP mode helpers ───────────────────────────────────────────
static void startAP() {
  _apConfigSaved = false;
  String macSuffix = WiFi.macAddress().substring(12);
  macSuffix.replace(":", "");
  String apSSID = String(AP_SSID_PREFIX) + "-" + macSuffix;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Log(NOTIFY, "AP: SSID=%s  IP=%d.%d.%d.%d  pass=%s\n",
      apSSID.c_str(), apIP[0], apIP[1], apIP[2], apIP[3], AP_PASSWORD);

  dnsServer.start(53, "*", apIP);   /* captive portal — redirect all DNS */

  apServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", AP_PAGE);
  });

  apServer.on("/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String ssid  = req->arg("ssid");
    String wpass = req->arg("wpass");
    String host  = req->arg("host");
    String mip   = req->arg("mip");
    String mport = req->arg("mport");
    String muser = req->arg("muser");
    String mpass = req->arg("mpass");

    if (ssid.length() == 0) {
      req->send(400, "text/plain", "SSID required");
      return;
    }
    SaveWifiToNVS(ssid.c_str(), wpass.c_str(),
                  host.length() ? host.c_str() : "LTE-Device");
    if (mip.length() > 0) {
      SaveMQTTToNVS(mip.c_str(), mport.toInt() ? mport.toInt() : 1883,
                    muser.c_str(), mpass.c_str(), 1);
    }
    Log(NOTIFY, "AP: config saved for %s — restarting\n", ssid.c_str());
    req->send_P(200, "text/html", AP_SAVED);
    _apConfigSaved = true;
  });

  apServer.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/");
  });

  apServer.begin();
}

static void stopAP() {
  dnsServer.stop();
  apServer.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  _apConfigSaved = false;
}

// ── Public boot sequence ──────────────────────────────────────
char WiFiBootSequence() {
  for (;;) {
    /* ── 1. Try NVS credentials ──────────────────────────────── */
    String ssid = GetSSID();
    String pass = GetSSIDPassword();

    if (ssid.length() > 0) {
      Log(NOTIFY, "WiFi: connecting to %s (30 s timeout)\n", ssid.c_str());
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid.c_str(), pass.c_str());
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 >= WIFI_CONNECT_MS) break;
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) {
        _haveWiFi = true;
        _status   = 1;
        CurrentIP = WiFi.localIP();
        timeClient.begin();
        Log(NOTIFY, "WiFi: connected — IP %d.%d.%d.%d\n",
            CurrentIP[0], CurrentIP[1], CurrentIP[2], CurrentIP[3]);
        return 1;
      }
      Log(ERROR, "WiFi: failed to connect to %s — starting AP\n", ssid.c_str());
      WiFi.disconnect(true);
    } else {
      Log(NOTIFY, "WiFi: no stored creds — starting AP\n");
    }

    /* ── 2. AP mode (5-min window) ───────────────────────────── */
    startAP();
    unsigned long apStart = millis();
    while (millis() - apStart < AP_TIMEOUT_MS) {
      dnsServer.processNextRequest();
      if (_apConfigSaved) {
        delay(1500);   /* let response reach the browser */
        ESP.restart();
      }
      delay(50);
    }
    Log(NOTIFY, "WiFi: AP timeout — retrying NVS creds\n");
    stopAP();
  }
}

// ── Accessors ─────────────────────────────────────────────────
void UpdateTime() {
  if (WiFi.status() == WL_CONNECTED) timeClient.update();
}

void PrintTime() {}

char CheckTime(char hour, char minute) {
  if (!timeClient.isTimeSet()) return -1;
  return (hour == timeClient.getHours() && minute == timeClient.getMinutes()) ? 1 : 0;
}

char GetWiFiStatus() { return _status; }

String GetIP() {
  return String(CurrentIP[0]) + "." + String(CurrentIP[1]) + "." +
         String(CurrentIP[2]) + "." + String(CurrentIP[3]);
}

String GetMAC()     { return WiFi.macAddress(); }
String GetRSSIStr() { return String(WiFi.RSSI()); }
