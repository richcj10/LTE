#include "WiFi.h"
#include <Arduino.h>
#include <WiFi.h>

const char *soft_ap_password = "LTEAP";
//const char* wifi_network_ssid = "Lights.Camera.Action";
//const char* wifi_network_password =  "RR58fa!8";
const char* ssid = "Lights.Camera.Action";
const char* password = "RR58fa!8";

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