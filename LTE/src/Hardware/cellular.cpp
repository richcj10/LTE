
#include "Adafruit_FONA.h"
#include <Arduino.h>
#include "cellular.h"
#include "Define.h"
#include "FuelGauge.h"
#include "LED.h"


//#define T_ALERT 12 // Connect with solder jumper


// this is a large buffer for replies
char replybuffer[255];

// Hardware serial is also possible!
//HardwareSerial *fonaSerial = &Serial1;
String urlencode(String str);

Adafruit_FONA_LTE fona = Adafruit_FONA_LTE();


uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
uint8_t type;
char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!

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
  
void LTEloop() {
  i++;
  //NetworkSetup();
  char ExistingColor = LEDGetValue();

  LEDUpdate(150);
  sprintf(msg, "Voltage = %f SoC = %f",GetCellV(),GetCellSoC());
  Pushsafer("Battery Status",msg);
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
    while (1); // Don't proceed if it couldn't find the device
  }

  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));
//  switch (type) {
//    case SIM800L:
//      Serial.println(F("SIM800L")); break;
//    case SIM800H:
//      Serial.println(F("SIM800H")); break;
//    case SIM808_V1:
//      Serial.println(F("SIM808 (v1)")); break;
//    case SIM808_V2:
//      Serial.println(F("SIM808 (v2)")); break;
//    case SIM5320A:
//      Serial.println(F("SIM5320A (American)")); break;
//    case SIM5320E:
//      Serial.println(F("SIM5320E (European)")); break;
//    case SIM7000:
//      Serial.println(F("SIM7000")); break;
//    case SIM7070:
//      Serial.println(F("SIM7070")); break;
//    case SIM7500:
//      Serial.println(F("SIM7500")); break;
//    case SIM7600:
//      Serial.println(F("SIM7600")); break;
//    default:
//      Serial.println(F("???")); break;
//  }

  // Print module IMEI number.
//  uint8_t imeiLen = fona.getIMEI(imei);
//  if (imeiLen > 0) {
//    Serial.print("Module IMEI: "); Serial.println(imei);
//  }
  fona.setNetworkSettings(F("hologram")); // For Hologram SIM card
  delay(5000);
  fona.enableGPRS(true);
  if (!fona.wirelessConnStatus()) {
    while (!fona.openWirelessConnection(true)) {
      Serial.println(F("Failed to enable connection, retrying..."));
      delay(2000); // Retry every 2s
    }
    Serial.println(F("Enabled data!"));
  }
  delay(100);
  fona.wirelessConnStatus();
}

void NetworkStop(){
  digitalWrite(FONA_RST, LOW);
  delay(1000);
  digitalWrite(FONA_RST, HIGH);
}

void NetworkTest(){
  uint8_t n = fona.GPRSstate();
  Serial.print(F("Network status "));
  Serial.print(n);
  Serial.print(F(": "));
  if (n == 0) Serial.println(F("Not registered"));
  if (n == 1) Serial.println(F("Registered (home)"));
  if (n == 2) Serial.println(F("Not registered (searching)"));
  if (n == 3) Serial.println(F("Denied"));
  if (n == 4) Serial.println(F("Unknown"));
  if (n == 5) Serial.println(F("Registered roaming"));
}

void Pushover(const char* Title, const char* Message){
  char URL[200];
  char BODY[200];
  sprintf(URL, "api.pushover.net");
  fona.HTTP_ssl(true);
  fona.HTTP_connect(URL);
  int SizeOfArray = sprintf(BODY, "token=ax54xhwax8om6q4hwtjxfa7qatanjc&user=ufi8weo5covwibzqg1a6iuzhpj1r3q&title=%s&message=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
  Serial.print("Array Body = ");Serial.println(SizeOfArray);
  Serial.print("Array Body = ");Serial.println(BODY);
  fona.HTTP_addHeader("Host","api.pushover.net",16);
  fona.HTTP_addHeader("Content-Type","application/json",16);
  fona.HTTP_addHeader("Content-Length","115",3);
  fona.HTTP_POST("/1/messages.json",BODY,SizeOfArray,4000);
}

void Pushsafer(const char* Title, const char* Message){
  char URL[200];
  char BODY[200];
  sprintf(URL, "pushsafer.com");
  //fona.HTTP_ssl(true);
  fona.HTTP_connect(URL);
  int SizeOfArray = sprintf(BODY, "k=2HKtWV9uyhxpSJ1Eigw1&t=%s&m=%s",urlencode(Title).c_str(),urlencode(Message).c_str());
  Serial.print("Array Body = ");Serial.println(SizeOfArray);
  Serial.print("Array Body = ");Serial.println(BODY);
  fona.HTTP_addHeader("Content-Type","application/x-www-form-urlencoded",33);
  //fona.HTTP_addHeader("Content-Length","140",3);
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