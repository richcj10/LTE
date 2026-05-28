#include "WiFi.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "Hardware/Log.h"
#include "Define.h"
#include "FileSystem/FSInterface.h"
#include "Functions.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

#define WIFI_CONNECT_MS  30000UL   /* 30 s to connect via NVS creds */
#define AP_TIMEOUT_MS   300000UL   /* 5 min before restart-to-retry  */

WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, "3.north-america.pool.ntp.org", 3600, 60000);

static const char AP_SSID_PREFIX[]  = "LTE-Setup";
static const char AP_PASSWORD[]     = "LTEAP";

static bool _haveWiFi = false;
static char _status   = 0;
IPAddress   CurrentIP;

static DNSServer      dnsServer;

static bool          _apMode      = false;
static unsigned long _apStartTime = 0;

// ── AP mode helpers ───────────────────────────────────────────
static void startAP() {
  _apMode      = true;
  _apStartTime = millis();
  String macSuffix = WiFi.macAddress().substring(12);
  macSuffix.replace(":", "");
  String apSSID = String(AP_SSID_PREFIX) + "-" + macSuffix;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Log(NOTIFY, "AP: SSID=%s  IP=%d.%d.%d.%d  pass=%s\n",
      apSSID.c_str(), apIP[0], apIP[1], apIP[2], apIP[3], AP_PASSWORD);
  /* Captive portal — redirect all DNS to the device IP so the browser opens the web UI */
  dnsServer.start(53, "*", apIP);
}

// ── Public boot sequence ──────────────────────────────────────
char WiFiBootSequence() {
  String ssid = GetSSID();
  String pass = GetSSIDPassword();

  if (ssid.length() > 0) {
    Log(NOTIFY, "WiFi: connecting to %s (30 s timeout)\n", ssid.c_str());
    WiFi.disconnect(true, true);
    delay(1000);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    String hn = GetUniqueName(); hn.replace(":", "");
    WiFi.setHostname(hn.c_str());
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

  /* No creds or connection failed — start AP, return immediately */
  startAP();
  return 2;
}

void WiFiAPProcess() {
  if (!_apMode) return;
  dnsServer.processNextRequest();
  /* Reset timeout while a client is connected so config session isn't interrupted */
  if (WiFi.softAPgetStationNum() > 0) {
    _apStartTime = millis();
    return;
  }
  /* If stored creds exist but connection failed, restart after timeout to retry */
  if (GetSSID().length() > 0 && (millis() - _apStartTime) > AP_TIMEOUT_MS) {
    Log(NOTIFY, "WiFi: AP timeout — restarting to retry\n");
    ESP.restart();
  }
}

bool WiFiIsAPMode() { return _apMode; }

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
