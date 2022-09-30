
#include <SIM7000.h>
#include <Arduino.h>
#include "cellular.h"
#include "Define.h"
#include "FuelGauge.h"
#include "LED.h"


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

void NetworkTest();
String urlencode(String str);
int freeRam();
void NetworkStop();

#define RXD2 17
#define TXD2 16

void LTEsetup() {
  //  while (!Serial);

  pinMode(FONA_RST, OUTPUT);
  pinMode(FONA_PWRKEY, OUTPUT);
  digitalWrite(FONA_RST, LOW);
  delay(1000);
  digitalWrite(FONA_RST, HIGH);
  
  NetworkSetup();

  //delay(1000);
  //Pushover("Hi Mike","This is a Test");
  //Pushsafer("Hi Rick","This is a Test");
}
long i = 0;
char counter = 0;
char msg[200];
char LTEerror = 0;
char LTEStatus = 0;
bool LTEConnected = 0;
  
void LTEloop() {
  i++;
  //NetworkSetup();
  char ExistingColor = LEDGetValue();

  LEDUpdate(150);
  sprintf(msg, "Voltage = %f SoC = %f",GetCellV(),GetCellSoC());
  Pushover("Battery Status",msg);
  LEDUpdate(ExistingColor);
}

void NetworkSetup(){
  fona.powerOn(FONA_PWRKEY); // Power on the module
  
  Serial.println(F("Initializing....(May take several seconds)"));

  // Software serial:
  Serial1.begin(115200, SERIAL_8N1, RXD2, TXD2); // Default SIM7000 shield baud rate

  //Serial.println(F("Configuring to 38400 baud"));
  //Serial1.println("AT+IPR=38400"); // Set baud rate
  //delay(100); // Short pause to let the command run
  //Serial1.begin(38400);
  if (! fona.begin(Serial1)) {
    Serial.println(F("Couldn't find FONA"));
    LTEerror = 1;
  }

  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));
  if(LTEerror == 0){
    fona.setNetworkSettings(F("hologram")); // For Hologram SIM card
    delay(1000);
    fona.enableGPRS(true);
    if (!fona.wirelessConnStatus()) {
      while (!fona.openWirelessConnection(true)) {
        Serial.println(F("Failed to enable connection, retrying..."));
        delay(1000); // Retry every 2s
        counter++;
        if(counter > 5){
          NetworkStop();
          break;
        }
      }
      Serial.println(F("Enabled data!"));
    }
    delay(100);
    LTEConnected = 1;
    fona.wirelessConnStatus();
  }
}

void NetworkStatusUpdate(){
  if(LTEConnected){
    LTEConnected = fona.wirelessConnStatus();
    LTEStatus = fona.GPRSstate();
  }
}

void NetworkStop(){
  digitalWrite(FONA_RST, LOW);
  delay(1000);
  digitalWrite(FONA_RST, HIGH);
}

char CheckSIM(){
  char CCID[32];
  int Lenth = fona.getSIMCCID(CCID);
}

void NetworkTest(){
  LTEStatus = fona.GPRSstate();
  Serial.print(F("Network status "));
  Serial.print(LTEStatus);
  Serial.print(F(": "));
  if (LTEStatus == 0) Serial.println(F("Not registered"));
  if (LTEStatus == 1) Serial.println(F("Registered (home)"));
  if (LTEStatus == 2) Serial.println(F("Not registered (searching)"));
  if (LTEStatus == 3) Serial.println(F("Denied"));
  if (LTEStatus == 4) Serial.println(F("Unknown"));
  if (LTEStatus == 5) Serial.println(F("Registered roaming"));
}

void Pushover(const char* Title, const char* Message){
  sprintf(URL, "api.pushover.net");
  fona.HTTP_ssl(true);
  fona.HTTP_connect(URL);
  int SizeOfArray = sprintf(BODY, "token=ax54xhwax8om6q4hwtjxfa7qatanjc&user=ufi8weo5covwibzqg1a6iuzhpj1r3q&title=%s&message=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
  //Serial.print("Array Body = ");Serial.println(SizeOfArray);
  //Serial.print("Array Body = ");Serial.println(BODY);
  fona.HTTP_POST("/1/messages.json",BODY,SizeOfArray,4000);
}

void Pushsafer(const char* Title, const char* Message){
  sprintf(URL, "pushsafer.com");
  fona.HTTP_ssl(true);
  fona.HTTP_connect(URL);
  int SizeOfArray = sprintf(BODY, "k=2HKtWV9uyhxpSJ1Eigw1&t=%s&m=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
  //Serial.print("Array Body = ");Serial.println(SizeOfArray);
  //Serial.print("Array Body = ");Serial.println(BODY);
  fona.HTTP_POST("/api",BODY,SizeOfArray,4000);
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
  Serial.println("LTE Debug:");
  Serial.print("LTE Connection: ");
  if (LTEStatus == 0) Serial.println(F("Not registered"));
  else if (LTEStatus == 1) Serial.println(F("Registered (home)"));
  else if (LTEStatus == 2) Serial.println(F("Not registered (searching)"));
  else if (LTEStatus == 3) Serial.println(F("Denied"));
  else if (LTEStatus == 4) Serial.println(F("Unknown"));
  else if (LTEStatus == 5) Serial.println(F("Registered roaming"));
  else Serial.println(F("N/A"));
  Serial.print("LTE Status: ");Serial.println(LTEConnected);
}