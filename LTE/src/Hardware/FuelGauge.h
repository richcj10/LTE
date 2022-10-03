#ifndef FUELGAUGE_H
#define  FUELGAUGE_H

#include <Arduino.h>

void FGsetup(char Debug);
void FGloop();
void FGDisplay();
float GetCellV();
float GetCellSoC();
bool GetCellAlert();
char GetFGerror();
String GetCellVString();
String GetCellSoCString();
String GetPowerModeString();

#endif 