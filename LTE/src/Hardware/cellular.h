#ifndef CELLULAR_H
#define  CELLULAR_H

void LTEsetup();
void LTEloop();
void Pushover(const char* Title, const char* Message);
void Pushsafer(const char* Title, const char* Message);
void NetworkSetup();
void NetworkTest();
void CellularDisplay();
void NetworkStatusUpdate();

#endif 