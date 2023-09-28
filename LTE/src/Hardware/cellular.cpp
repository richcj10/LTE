
#include <SIM7000.h>
#include <Arduino.h>
#include "cellular.h"
#include "Define.h"
#include "FuelGauge.h"
#include "LED.h"
#include "Log.h"

//https://support.hologram.io/hc/en-us/articles/360036559494-SIMCOM-SIM7000


// this is a large buffer for replies
char replybuffer[255];

// Hardware serial is also possible!
//HardwareSerial *fonaSerial = &Serial1;
String urlencode(String str);

Adafruit_FONA_LTE fona = Adafruit_FONA_LTE();


uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
uint8_t type;
char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!
char URL[200];
char BODY[200];

bool LTEon = 0;
char LTEMode = 0;
char LTERequestedMode = 0;
long i = 0;
char counter = 0;
char msg[200];
char LTEerror = 0;
char LTEStatus = 0;
int8_t Rssi = 0;
bool LTEConnected = 0;
String Status = "N/A";


void NetworkTest();
String urlencode(String str);
int freeRam();
void NetworkStop();

#define RXD2 17
#define TXD2 16

void LTEsetup(){
  pinMode(FONA_RST, OUTPUT);
  pinMode(FONA_PWRKEY, OUTPUT);
  digitalWrite(FONA_RST, LOW);
  delay(1000);
  digitalWrite(FONA_RST, HIGH);
  fona.powerOn(FONA_PWRKEY); // Power on the module
  
  //Serial.println(F("Initializing....(May take several seconds)"));
  Log(NOTIFY,"Starting LTE Radio\r\n");

  // Software serial:
  Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2); // Default SIM7000 shield baud rate

  if (! fona.begin(Serial1)) {
    //Serial.println(F("Couldn't find FONA"));
    Log(ERROR,"LTE Radio Not Found");
    LTEerror = 1;
    LTEon = 0;
  }
  else{
    type = fona.type();
    Log(NOTIFY,"LTE Radio OK\r\n");
    LTEon = 1;
  }
}
  
void LTEControl() {
  if(LTEon){
    NetworkSetup();
    char ExistingColor = LEDGetValue();
    LEDColor(150);
    sprintf(msg, "Voltage = %0.1f SoC = %0.1f",GetCellV(),GetCellSoC());
    Pushover("Battery Status",msg);
    LEDColor(ExistingColor);
    NetworkStop();
    delay(100);
    //SendTextMsg();
  }
  else{
    LTEsetup();
    NetworkSetup();
    char ExistingColor = LEDGetValue();
    LEDColor(150);
    sprintf(msg, "Voltage = %0.2f SoC = %0.2f",GetCellV(),GetCellSoC());
    if(Pushover("Battery Status",msg)){
      LEDColor(96);
      delay(100);
    }
    LEDColor(ExistingColor);
    NetworkStop();
    delay(100);
    //SendTextMsg();
  }
}

void NetworkSetup(){
  if(LTEerror == 0){
    LTEConnected = 1;
    //Log(NOTIFY,"Holo\n");
    fona.setNetworkSettings(F("hologram")); // For Hologram SIM card
    delay(1000);
    fona.enableGPRS(true);
    CheckSIM();
    if (!fona.wirelessConnStatus()) {
      while (!fona.openWirelessConnection(true)) {
        //Serial.println(F("Failed to enable connection, retrying..."));
        Log(NOTIFY,"LTE Connection Retry\n");
        delay(1000); // Retry every 2s
        counter++;
        if(counter > 24){
          Log(ERROR,"LTE Connection Fail\n");
          LTEConnected = 0;
          NetworkStop();
          break;
        }
      }
      //Serial.println(F("Enabled data!"));
      Log(NOTIFY,"Data OK\n");
    }
    delay(100);
    fona.wirelessConnStatus();
  }
}

void NetworkStatusUpdate(){
  if(LTEConnected){
    LTEStatus = fona.wirelessConnStatus();
    //LTEState = fona.GPRSstate();
  }
}

void NetworkStop(){
  if(LTEon == 1){
    fona.enableGPRS(false);
  }
  //digitalWrite(FONA_RST, LOW);
  //delay(1000);
  //digitalWrite(FONA_RST, HIGH);
  //counter = 0;
  LTEStatus = 0;
}

void NetworkReset(){
  if(LTEon == 1){
    fona.enableGPRS(false);
  }
  delay(1000);
  digitalWrite(FONA_RST, LOW);
  delay(1000);
  digitalWrite(FONA_RST, HIGH);
  counter = 0;
  LTEStatus = 0;
}

char CheckSIM(){
  char CCID[32];
  int Lenth = fona.getSIMCCID(CCID);
  for(char i = 0;i< Lenth;i++){
    Serial.print(CCID[i]);
  }
  Serial.println();
  return 0;
}

void NetworkTest(){
  if(LTEon){
    LTEStatus = fona.GPRSstate();
    uint8_t n = fona.getRSSI();

    //Serial.print(F("RSSI = ")); Serial.print(n); Serial.print(": ");
    if (n == 0) Rssi = -115;
    if (n == 1) Rssi = -111;
    if (n == 31) Rssi = -52;
    if ((n >= 2) && (n <= 30)) {
      Rssi = map(n, 2, 30, -110, -54);
    }
    //Serial.print(r); Serial.println(F(" dBm"));
    //Serial.print(F(": "));
    if (LTEStatus == 0){
      Status = "Not registered\r\n";
      //Serial.println(F("Not registered"));
    }
    if (LTEStatus == 1){
      Status = "Registered (home)\r\n";
      //Serial.println(F("Registered (home)"));
    }
    if (LTEStatus == 2){
      Status = "Not registered (searching)\r\n";
      //Serial.println(F("Not registered (searching)"));
    }
    if (LTEStatus == 3){
      Status = "Denied\r\n";
      //Serial.println(F("Denied"));
    }
    if (LTEStatus == 4){
      Status = "Unknown\r\n";
      //Serial.println(F("Unknown"));
    }
    if (LTEStatus == 5){
      Status = "Registered roaming\r\n";
      //Serial.println(F("Registered roaming"));
    }
  }
}

char CellStateController(){
  switch(LTEMode){
    case 1:
      break;
    case 2:
      break;
    case 3:
      break; 
  }
}

bool Pushover(const char* Title, const char* Message){
  SetLEDStatus(LED_DEFAULT);
  char ExistingColor = LEDGetValue();
  LEDColor(150);
  if(!LTEon){
    LTEsetup();
  }
  NetworkSetup();
  if(LTEConnected){
    //Log(NOTIFY,"POVR Sending");
    sprintf(URL, "api.pushover.net");
    fona.HTTP_ssl(true);
    fona.HTTP_connect(URL);
    //Serial.println(Title);
    int SizeOfArray = sprintf(BODY, "token=ax54xhwax8om6q4hwtjxfa7qatanjc&user=ufi8weo5covwibzqg1a6iuzhpj1r3q&title=%s&message=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
    //Serial.print("Array Body = ");Serial.println(SizeOfArray);
    //Serial.print("Array Body = ");Serial.println(BODY);
    char value = fona.HTTP_POST("/1/messages.json",BODY,SizeOfArray,4000);
    Log(NOTIFY,"PSOVR Sent, Result = %d",value);
    NetworkStop();
    LEDColor(ExistingColor);
    return 1;
  }
  else{
    Log(ERROR,"POVR: No LTE Con");
    NetworkStop();
    LEDColor(ExistingColor);
    return 0;
  }
}

bool Pushsafer(const char* Title, const char* Message){
  if(LTEConnected){
    Log(NOTIFY,"PSVR Sending");
    sprintf(URL, "pushsafer.com");
    fona.HTTP_ssl(true);
    fona.HTTP_connect(URL);
    int SizeOfArray = sprintf(BODY, "k=2HKtWV9uyhxpSJ1Eigw1&t=%s&m=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
    //Serial.print("Array Body = ");Serial.println(SizeOfArray);
    //Serial.print("Array Body = ");Serial.println(BODY);
    char value = fona.HTTP_POST("/api",BODY,SizeOfArray,4000);
    Log(NOTIFY,"PSFR Sent, Result = %d",value);
    return 1;
  }
  else{
    Log(ERROR,"PSVR: No LTE Con");
    return 0;
  }
}

bool SendTextMsg(){
  //fona.sendSMS((char *)"13306049357", (char *)"Hey, I got your text!");
  return 0;
}

String urlencode(String str){
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      //yield(); - Use for ESPxx based processors. 
    }
    return encodedString;
    
}

void CellularDisplay(){
  if(LTEon){
    Log(LOG,"LTE: ON\n");
    Log(LOG,"LTE: Connected = %d\n",LTEConnected);
    Log(LOG,Status.c_str());
  }
  else{
    Log(LOG,"LTE: OFF\n");
  }
}

String CellStatString(){
  return Status;
}

String CellSIMString(){
  return "OK";
}

String CellSigString(){
  return String(Rssi);
}

int8_t CellSig(){
  return Rssi;
}

String CellNetworkString(){
  return "hologram";
}

String CellIPString(){
  return "10.10.80.5";
}

String CellGPSString(){
 return "OFF";
}

String CellLATString(){
  return "N/A";
}

String CellLOGString(){
  return "N/A";
}