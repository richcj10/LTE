#include "MQTT.h"
#include "Functions.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "Wifi.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"

#define MQTTid "CellDevice"
#define MQTTip "192.168.10.101"
#define MQTTport 1883
#define MQTTuser "ESPPLCCell"
#define MQTTpsw "ESPPLCpswd"
#define MQTTpubQos 1
#define MQTTsubQos 1

#define MSG_BUFFER_SIZE 50
//char msg[MSG_BUFFER_SIZE];
char MQTTActive = 0;
char MQTTLockout = 0;
char temp[50]; 
char MsgType = 0;

WiFiClient wclient;
PubSubClient client(wclient);
StaticJsonDocument<200> jsonmqttRx;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  if(strcmp(topic,"Cellular/CellMsg") == 0){
    //Serial.println("CellMsg");
    deserializeJson(jsonmqttRx, payload);
    //Serial.println("jSON PARSE");
    //const char* MsgTxLoc = jsonmqttRx["Loc"];
    const char* MsgTitle = jsonmqttRx["Title"];
    const char* MsgTxLoc = jsonmqttRx["Loc"];
    const char* Msg = jsonmqttRx["Msg"];
    Serial.print("Message title ");
    Serial.print(MsgTitle);
    if(strcmp(MsgTxLoc,"PO") == 0){
      Serial.println(" PO");
      client.publish("Cellular/Status","Sending PO...");
      bool result = Pushover(MsgTitle,Msg);
      if(result){
        client.publish("Cellular/Status","Msg OK");
      }
      else{
        client.publish("Cellular/Status","Msg Failed");
      }
    }
  }
}

void MqttLoop(void){
  if(MQTTLockout == 0){
    if(client.connected()){
      client.loop();
    }
    else{
      MQTTreconnect();
    }
  }
}

void MQTTStart(){
  client.setServer(MQTTip, 1883);
  client.setCallback(callback);
}

void MQTTreconnect(void) {
  // Loop until we're reconnected
  char counter = 0;
    if(GetWiFiStatus() == 1){
      while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if(client.connect(MQTTid,MQTTuser,MQTTpsw)) {
          MQTTActive = 1;
          Serial.println("MQTT connected!");
          client.subscribe("Cellular/CellMsg");
          client.subscribe("Cellular/CellCMD");
          MQTTMessageUpdate();
       }
       counter++;
       if(counter > 2){
         Serial.println("MQTT ISSUE");
         MQTTLockout = 1;
         break;
       }
     }
   }
}

void MQTTMessageUpdate(){
  client.publish("Cellular/CellPWR",GetCellSoCString(1).c_str());
  client.publish("Cellular/CellStatus",CellStatString().c_str());
  client.publish("Cellular/CellSignal",CellSigString().c_str());
}

char GetMQTTStatus(void){
  return MQTTActive;
}

char GetMQTTMsg(){
  return MsgType;
}

void CLRMQTTMsg(){
  MsgType = 0;
}