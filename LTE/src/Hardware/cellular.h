#ifndef CELLULAR_H
#define CELLULAR_H

#include <Arduino.h>

/* Start the FreeRTOS cellular task — call once in setup() */
void CellTaskStart();

/* Non-blocking Pushover — queues the notification, returns true if queued */
bool Pushover(const char* title, const char* message);

/* GPS control */
void   GPSenable(bool on);
bool   GPSisEnabled();
bool   GPShasFix();
float  GPSlat();
float  GPSlon();
float  GPSspeed();
float  GPSheading();
float  GPSaltitude();
String GPSlatString();
String GPSlonString();

/* State accessors (thread-safe) */
bool   CellIsOn();
bool   CellIsConnected();
int8_t CellSig();
String CellStateStr();
String CellStatString();
String CellSIMString();
String CellSigString();
String CellNetworkString();
String CellIPString();
String CellGPSString();
String CellLATString();
String CellLONString();
void   CellularDisplay();
bool   SendTextMsg(const char* number, const char* message);

/* Last Pushover result — titleOut[48], statusOut[40] */
void   CellLastPushover(char* titleOut, bool* okOut, char* statusOut);

#endif
