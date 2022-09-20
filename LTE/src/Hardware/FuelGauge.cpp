#include "FuelGauge.h"

#include <Wire.h> // Needed for I2C

#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h> // Click here to get the library: http://librarymanager/All#SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library

SFE_MAX1704X lipo(MAX1704X_MAX17048); // Create a MAX17048

float voltage = 0; // Variable to keep track of LiPo voltage
float soc = 0; // Variable to keep track of LiPo state-of-charge (SOC)
bool alert; // Variable to keep track of whether alert has been triggered
char Error = 0;

void FGsetup(char Debug){
    if(Debug){
        lipo.enableDebugging(); // Uncomment this line to enable helpful debug messages on Serial
    }
    // Set up the MAX17044 LiPo fuel gauge:
    if (lipo.begin() == false){
        Serial.println(F("MAX17044 not detected. Please check wiring. Freezing."));
        Error = 1;
    }

	// Quick start restarts the MAX17044 in hopes of getting a more accurate
	// guess for the SOC.
	lipo.quickStart();

	// We can set an interrupt to alert when the battery SoC gets too low.
	// We can alert at anywhere between 1% - 32%:
	lipo.setThreshold(10); // Set alert threshold to 20%.
}

void FGloop(){
	// lipo.getVoltage() returns a voltage value (e.g. 7.86)
	voltage = lipo.getVoltage();
	// lipo.getSOC() returns the estimated state of charge (e.g. 79%)
	soc = lipo.getSOC();
	// lipo.getAlert() returns a 0 or 1 (0=alert not triggered)
	alert = lipo.getAlert();
}

void FGDisplay(){
	Serial.print("Voltage: ");
	Serial.print(voltage);  // Print the battery voltage
	Serial.println(" V");

	Serial.print("Percentage: ");
	Serial.print(soc); // Print the battery state of charge
	Serial.println(" %");

    Serial.print("Alert: ");
    Serial.println(alert);
    Serial.println();
}

float GetCellV(){
	return voltage;
}

float GetCellSoC(){
	return soc;
}

bool GetCellAlert(){
	return alert;
}