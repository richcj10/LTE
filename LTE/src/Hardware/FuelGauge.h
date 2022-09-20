#ifndef FUELGAUGE_H
#define  FUELGAUGE_H

void FGsetup(char Debug);
void FGloop();
void FGDisplay();
float GetCellV();
float GetCellSoC();
bool GetCellAlert();

#endif 