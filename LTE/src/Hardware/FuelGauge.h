#ifndef FUELGAUGE_H
#define  FUELGAUGE_H

#include <Arduino.h>

#define ACON 1
#define BUBON 2

void FGsetup(char Debug);
void FGloop();
void FGDisplay();
float GetCellV();
float GetCellSoC();
bool GetCellAlert();
char GetFGerror();
char GetPowerMode();
float GetCellRate();

String GetCellVString();
String GetCellSoCString(bool Round);
String GetPowerModeString();
String GetCellRateString();

#endif 