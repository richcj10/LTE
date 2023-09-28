#ifndef LED_H
#define  LED_H

void LEDsetup();
void LEDBrightness(char Value);
char LEDGetValue();
void LEDColor(char Value);
void LEDUpdate();
void SetLEDStatus(char type);
void SetLEDStatus(char type, int rate);
char GetLEDMode(void);

#define LED_DEFAULT 10
#define LED_BLINK 11
#define LED_FLASH 12

#endif 