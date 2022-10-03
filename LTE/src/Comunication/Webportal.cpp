#include <ArduinoJson.h>
#include "FS.h"
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FileSystem/FSInterface.h"
#include "Wifi.h"
#include "Define.h"
#include "Hardware/Log.h"
#include "Functions.h"
#include "Hardware/FuelGauge.h"
#include "Hardware/cellular.h"
#include "Hardware/Log.h"

#define HTTP_PORT 80

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");

StaticJsonDocument<200> jsonDocTx;
StaticJsonDocument<100> jsonDocRx;

bool wsconnected = false;
bool lastButtonState = 0;
static char output[512];

unsigned long cnt = 0;
unsigned long LastTime = 0;

char JsonType = 0;
bool Firstupdate = 0;
int WebHandelTime = 0;

void notFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    wsconnected = true;
    Firstupdate = 1;
    jsonDocTx["Type"] = 0;
    jsonDocTx["SSID"] = GetSSID();
    jsonDocTx["IP"] = GetIP();
    jsonDocTx["HN"] = GetUniqueName();
    jsonDocTx["MAC"] = GetMAC();

    serializeJson(jsonDocTx, output, 512);

    if (ws.availableForWriteAll()) {
      ws.textAll(output);
      Log(NOTIFY,"Sent");
    } 
    else {
      Log(ERROR,"Queue Is Full");
    }
    client->ping();
  } else if (type == WS_EVT_DISCONNECT) {
    wsconnected = false;
    //LOG("ws[%s][%u] disconnect\n", server->url(), client->id());
  } else if (type == WS_EVT_ERROR) {
    ///LOG("ws[%s][%u] error(%u): %s\n", server->url(), client->id(),
    //    *((uint16_t*)arg), (char*)data);
  } else if (type == WS_EVT_PONG) {
    //LOG("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len,
        //(len) ? (char*)data : "");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len) {
      // the whole message is in a single frame and we got all of it's data
      //LOG("ws[%s][%u] %s-msg[%llu]\r\n", server->url(), client->id(),
          //(info->opcode == WS_TEXT) ? "txt" : "bin", info->len);

      if (info->opcode == WS_TEXT) {
        for (size_t i = 0; i < info->len; i++) {
          msg += (char)data[i];
        }
        //LOG("%s\r\n\r\n", msg.c_str());

        deserializeJson(jsonDocRx, msg);

        //uint8_t ledState = jsonDocRx["led"];
        jsonDocRx.clear();
      }
    }
  }
}

void WebStart(){
  /* Start web server and web socket server */
  //lastButtonState = digitalRead(USER_SW);
  /* Start web server and web socket server */
  Log(NOTIFY,"Web Service Start!\r");
   server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    Serial.print("Root Page");
    //request->send(200, "text/html", "OK");
    request->send(LittleFS, "/Main.html", "text/html");
  });
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.print("got unhandled request for ");
    Serial.println(request->url());
    request->send(404);
  });
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();
}


void WebHandel(){
  if((millis() - LastTime) > WebHandelTime){
    LastTime = millis();
    if(wsconnected == true){
      if(Firstupdate == 1) WebHandelTime = 100; //Send data for quick update
      jsonDocTx.clear();
      jsonDocTx["Type"] = JsonType;
      if(JsonType == 1){
        jsonDocTx["RSSI"] = GetRSSIStr();
        jsonDocTx["IO1"] = 1;
        jsonDocTx["IO2"] = 1;
      }

      if(JsonType == 2){
        jsonDocTx["CELST"] = CellStatString();
        jsonDocTx["CELIP"] = CellIPString();
        jsonDocTx["CELSIG"] = CellSigString();
        jsonDocTx["CELNT"] = CellNetworkString();
        jsonDocTx["GPSEN"] = CellGPSString();
        jsonDocTx["GPSLAT"] = CellLATString();
        jsonDocTx["GPSLOG"] = CellLOGString();
        jsonDocTx["SIM"] = CellSIMString();
      }

      if(JsonType == 3){
        jsonDocTx["BATV"] = GetCellVString();
        jsonDocTx["BATP"] = GetCellSoCString();
        jsonDocTx["PWRMD"] = GetPowerModeString();
        Firstupdate = 0;
        WebHandelTime = 500;
      }

      serializeJson(jsonDocTx, output, 512);

      if (ws.availableForWriteAll()) {
        ws.textAll(output);
        //Log(NOTIFY,"Sent");
      } 
      else {
        Log(ERROR,"Queue Is Full");
      }
    }
    JsonType++;
    if(JsonType >= 4){
      JsonType = 1;
    }
  }
}

char WebLogSend(String LogString){
  if(wsconnected == true){
    //Serial.println(LogString);
    jsonDocTx.clear();
    jsonDocTx["Type"] = 10; //Log Send Command
    jsonDocTx["LOG"] = LogString + "\n";//\n";
    serializeJson(jsonDocTx, output, 512);
    if (ws.availableForWriteAll()) {
      ws.textAll(output);
        //Log(NOTIFY,"Sent Log");
    } 
/*       else {
        Log(ERROR,"Queue Is Full");
      } */
    return 1;
  }
  return 0;
}
