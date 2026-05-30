/**
 * @file Wifi.cpp
 * @brief WiFi STA connection with captive-portal AP fallback.
 *
 * Connection timeout: 30 s.  AP idle timeout before auto-restart: 5 min.
 * AP SSID: @c LTE-Setup-XXXX (last 4 hex chars of MAC), password: @c LTEAP.
 */
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

#define WIFI_CONNECT_MS  30000UL
#define AP_TIMEOUT_MS   300000UL

static WiFiUDP   ntpUDP;
static NTPClient timeClient(ntpUDP, "3.north-america.pool.ntp.org", 3600, 60000);

static const char AP_SSID_PREFIX[] = "LTE-Setup";
static const char AP_PASSWORD[]    = "LTEAP";

static bool _haveWiFi = false;
static char _status   = 0;
static IPAddress CurrentIP;

static DNSServer      dnsServer;
static bool          _apMode      = false;
static unsigned long _apStartTime = 0;

/**
 * @brief Start captive-portal AP mode.
 *
 * Sets WIFI_AP mode, derives the SSID from the MAC address suffix,
 * and starts a DNS server that redirects all queries to the device IP.
 */
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
  dnsServer.start(53, "*", apIP);
}

/**
 * @brief Attempt STA connection using NVS credentials; fall back to AP on failure.
 * @return @c 1 = STA connected and NTP started; @c 2 = AP mode started.
 */
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

  startAP();
  return 2;
}

/**
 * @brief Service captive-portal DNS and AP idle-restart timer.
 *
 * Resets the idle timer while a station is associated so an active
 * configuration session is not interrupted.
 */
void WiFiAPProcess() {
  if (!_apMode) return;
  dnsServer.processNextRequest();
  if (WiFi.softAPgetStationNum() > 0) {
    _apStartTime = millis();
    return;
  }
  if (GetSSID().length() > 0 && (millis() - _apStartTime) > AP_TIMEOUT_MS) {
    Log(NOTIFY, "WiFi: AP timeout — restarting to retry\n");
    ESP.restart();
  }
}

/** @return @c true while operating as an access point. */
bool WiFiIsAPMode() { return _apMode; }

/** @brief Trigger an NTP client update (no-op when WiFi is not connected). */
void UpdateTime() {
  if (WiFi.status() == WL_CONNECTED) timeClient.update();
}

/** @brief Legacy stub — no-op. */
void PrintTime() {}

/**
 * @brief Check whether the current NTP time matches hour:minute.
 * @param hour   Hour to match (0–23).
 * @param minute Minute to match (0–59).
 * @return @c 1 if matched, @c 0 if not, @c -1 if NTP time is unavailable.
 */
char CheckTime(char hour, char minute) {
  if (!timeClient.isTimeSet()) return -1;
  return (hour == timeClient.getHours() && minute == timeClient.getMinutes()) ? 1 : 0;
}

/** @return @c 1 when WiFi STA is connected. */
char GetWiFiStatus() { return _status; }

/** @return Current STA IP as a dotted-decimal string. */
String GetIP() {
  return String(CurrentIP[0]) + "." + String(CurrentIP[1]) + "." +
         String(CurrentIP[2]) + "." + String(CurrentIP[3]);
}

/** @return WiFi MAC address string. */
String GetMAC()     { return WiFi.macAddress(); }

/** @return WiFi RSSI as a decimal string. */
String GetRSSIStr() { return String(WiFi.RSSI()); }
