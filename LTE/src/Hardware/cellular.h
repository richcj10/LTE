#ifndef CELLULAR_H
#define  CELLULAR_H

#include <Arduino.h>

void LTEsetup();
void LTEControl();
bool Pushover(const char* Title, const char* Message);
bool Pushsafer(const char* Title, const char* Message);
void NetworkSetup();
void NetworkTest();
void NetworkStop();
void NetworkReset();
void CellularDisplay();
void NetworkStatusUpdate();
char CheckSIM();
bool SendTextMsg();
String CellStatString();
String CellSigString();
String CellNetworkString();
String CellIPString();
String CellSIMString();
String CellGPSString();
String CellLATString();
String CellLOGString();
int8_t CellSig();

#endif 