#ifndef FUNCTIONS_H
#define  FUNCTIONS_H

#include <Arduino.h>

char Startup(bool WifiMode, bool LTEMode);
void RunLoop();
void DebugPrint();
void UniqueName();
void WiFiNetworkSetup();
void LTELoop();
String GetUniqueName();

#endif 