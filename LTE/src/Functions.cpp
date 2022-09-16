#include "Functions.h"
#include <Arduino.h>
#include <Wire.h>
#include "Hardware/LED.h"
#include "Hardware/FuelGauge.h"

void ConfigIO(){
    Serial.begin(115200);
    Wire.begin(27,26);
}

char Startup(){
    ConfigIO();
    LEDsetup();
    FGsetup(1);
    return 1;
}

void RunLoop(){
    FGloop();
}

void DebugPrint(){
    FGDisplay();
}