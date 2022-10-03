#ifndef CELLULAR_H
#define  CELLULAR_H

#include <Arduino.h>

void LTEsetup();
void LTEloop();
void Pushover(const char* Title, const char* Message);
void Pushsafer(const char* Title, const char* Message);
void NetworkSetup();
void NetworkTest();
void NetworkStop();
void CellularDisplay();
void NetworkStatusUpdate();
String CellStatString();
String CellSigString();
String CellNetworkString();
String CellIPString();
String CellSIMString();
String CellGPSString();
String CellLATString();
String CellLOGString();

#endif 