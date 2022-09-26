#include "WiFi.h"
#include <Arduino.h>
#include <WiFi.h>

const char *soft_ap_password = "LTEAP";
//const char* wifi_network_ssid = "**REDACTED-SSID**";
//const char* wifi_network_password =  "**REDACTED-PASS**";
const char* ssid = "**REDACTED-SSID**";
const char* password = "**REDACTED-PASS**";

void WiFiSetup(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}