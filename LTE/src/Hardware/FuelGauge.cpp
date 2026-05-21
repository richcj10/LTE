#include "FuelGauge.h"

#include <Wire.h> // Needed for I2C
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h> // Click here to get the library: http://librarymanager/All#SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library
#include "Log.h"

SFE_MAX1704X lipo(MAX1704X_MAX17048); // Create a MAX17048

float voltage = 0; // Variable to keep track of LiPo voltage
float soc = 0; // Variable to keep track of LiPo state-of-charge (SOC)
bool alert; // Variable to keep track of whether alert has been triggered
char FGerror = 0;
float CellChangeRate = 0;
char BattMode = 0;
char ACPwrMode = ACON;

void FGsetup(char Debug){
    if(Debug){
        lipo.enableDebugging(); // Uncomment this line to enable helpful debug messages on Serial
    }
    // Set up the MAX17044 LiPo fuel gauge:
    if (lipo.begin() == false){
        Serial.println(F("MAX17044 not detected. Please check wiring. Freezing."));
        FGerror = 1;
    }

	// Quick start restarts the MAX17044 in hopes of getting a more accurate
	// guess for the SOC.
	lipo.quickStart();

	// We can set an interrupt to alert when the battery SoC gets too low.
	// We can alert at anywhere between 1% - 32%:
	lipo.setThreshold(10); // Set alert threshold to 20%.
}

void FGloop(){
	if(FGerror == 0){
		voltage = lipo.getVoltage();
		soc = lipo.getSOC();
		alert = lipo.getAlert();
		CellChangeRate = lipo.getChangeRate();
		if(ACPwrMode == ACON){
			if(CellChangeRate < -6.0){
				BattMode++;
				//Log(LOG," CR< 4 BattMode  = %d\n\r",BattMode);
				if(BattMode > 30){ //30 sensonds of BUB battery 
					ACPwrMode = BUBON;
					BattMode = 0;
				}
			}
			else{
				BattMode = 0;
			}
		}
		if(ACPwrMode == BUBON){
			if(CellChangeRate > -4.1){
				BattMode++;
				//Log(LOG," CR> 4 BattMode  = %d\n\r",BattMode);
				if(BattMode > 30){ //30 sensonds of AC 
					ACPwrMode = ACON;
					BattMode = 0;
				}
			}
			else{
				BattMode = 0;
			}
		}
	}
	else{
		voltage = 0;
		soc = 0;
		alert = 0;
		CellChangeRate = 0;
	}
}

void FGDisplay(){
	if(FGerror == 0){
		//Serial.print("Voltage: ");
		//Serial.print(voltage);  // Print the battery voltage
		//Serial.println(" V");

		//Serial.print("Percentage: ");
		//Serial.print(soc); // Print the battery state of charge
		//Serial.println(" %");

		//Serial.print("Alert: ");
		//Serial.println(alert);
		//Serial.println();
		Log(LOG," Cell V = %.1f\n\r",voltage);
		Log(LOG," Cell SOC = %.1f\n\r",soc);
		Log(LOG," Cell Change Rate = %.1f\n\r",CellChangeRate);
	}
	else{
		//Serial.print("No FG");
		//Serial.println();
		Log(ERROR,"NoFG");
	}
}

float GetCellV(){
	return voltage;
}

float GetCellSoC(){
	return soc;
}

float GetCellRate(){
	return CellChangeRate;
}

String GetCellRateString(){
	return String(CellChangeRate,1);
}

String GetCellVString(){
	return String(voltage,2);
}

String GetCellSoCString(bool Round){
	if(Round == 1){
		if(soc < 100.0){
			return String(soc,2);
		}
		else{
			return String(100.0,2);
		}
	}
	return String(soc,2);
}

String GetPowerModeString(){
	if(ACPwrMode == ACON){
		return "AC";
	}
	else if(ACPwrMode == BUBON){
		return "BUB";
	}
	else{
		return "NA";
	}
}

char GetPowerMode(){
	return ACPwrMode;
}

bool GetCellAlert(){
	return alert;
}

char GetFGerror(){
	return FGerror;
}