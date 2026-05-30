/**
 * @file FuelGauge.cpp
 * @brief MAX17048 LiPo fuel-gauge driver — voltage, SoC, rate, and power-mode detection.
 *
 * Power-mode hysteresis: transitions to BUBON after 30 consecutive FGloop() calls
 * with a charge rate below –6 %/hr, and back to ACON after 30 calls above –4.1 %/hr.
 */
#include "FuelGauge.h"
#include <Wire.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include "Log.h"

static SFE_MAX1704X lipo(MAX1704X_MAX17048);

static float voltage        = 0;
static float soc            = 0;
static bool  alert          = false;
static char  FGerror        = 0;
static float CellChangeRate = 0;
static char  BattMode       = 0;
static char  ACPwrMode      = ACON;

/**
 * @brief Initialise the MAX17048, run a quick-start, and set the alert threshold.
 * @param Debug Non-zero enables SparkFun library debug output on Serial.
 */
void FGsetup(char Debug) {
    if (Debug) lipo.enableDebugging();
    if (lipo.begin() == false) {
        Serial.println(F("MAX17044 not detected. Please check wiring. Freezing."));
        FGerror = 1;
    }
    lipo.quickStart();
    lipo.setThreshold(10);
}

/**
 * @brief Poll voltage, SoC, alert flag, and charge rate; update power-mode state machine.
 */
void FGloop() {
    if (FGerror == 0) {
        voltage        = lipo.getVoltage();
        soc            = lipo.getSOC();
        alert          = lipo.getAlert();
        CellChangeRate = lipo.getChangeRate();

        if (ACPwrMode == ACON) {
            if (CellChangeRate < -6.0) {
                if (++BattMode > 30) { ACPwrMode = BUBON; BattMode = 0; }
            } else {
                BattMode = 0;
            }
        }
        if (ACPwrMode == BUBON) {
            if (CellChangeRate > -4.1) {
                if (++BattMode > 30) { ACPwrMode = ACON; BattMode = 0; }
            } else {
                BattMode = 0;
            }
        }
    } else {
        voltage = 0; soc = 0; alert = 0; CellChangeRate = 0;
    }
}

/** @brief Print voltage, SoC, and rate to the log at DEBUG level. */
void FGDisplay() {
    if (FGerror == 0) {
        Log(DEBUG, " Cell V = %.1f\n\r",          voltage);
        Log(DEBUG, " Cell SOC = %.1f\n\r",         soc);
        Log(DEBUG, " Cell Change Rate = %.1f\n\r", CellChangeRate);
    } else {
        Log(ERROR, "NoFG");
    }
}

/** @return Battery terminal voltage in volts. */
float GetCellV()    { return voltage; }

/** @return Battery state-of-charge in percent. */
float GetCellSoC()  { return soc; }

/** @return Instantaneous charge/discharge rate in %/hr. */
float GetCellRate() { return CellChangeRate; }

/** @return Charge rate formatted to one decimal place. */
String GetCellRateString() { return String(CellChangeRate, 1); }

/** @return Battery voltage formatted to two decimal places. */
String GetCellVString() { return String(voltage, 2); }

/**
 * @brief Battery SoC formatted to two decimal places.
 * @param Round When @c true, clamp to @c "100.00" at full charge.
 */
String GetCellSoCString(bool Round) {
    if (Round && soc >= 100.0f) return String(100.0, 2);
    return String(soc, 2);
}

/** @return Power-mode label: @c "AC", @c "BUB", or @c "NA". */
String GetPowerModeString() {
    if (ACPwrMode == ACON)  return "AC";
    if (ACPwrMode == BUBON) return "BUB";
    return "NA";
}

/** @return Current power-source code: ACON or BUBON. */
char GetPowerMode()   { return ACPwrMode; }

/** @return @c true when the low-battery alert has triggered. */
bool GetCellAlert()   { return alert; }

/** @return Non-zero when the fuel gauge failed to initialise. */
char GetFGerror()     { return FGerror; }
