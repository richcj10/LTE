/**
 * @file FuelGauge.h
 * @brief MAX17048 LiPo fuel-gauge driver interface.
 *
 * Wraps the SparkFun MAX1704x library.  Tracks battery voltage,
 * state-of-charge (SoC), and charge/discharge rate over I²C.  Also
 * runs a hysteresis-based state machine that classifies the power source
 * as AC-connected (ACON) or running on backup battery (BUBON) by watching
 * sustained changes in charge rate.
 */
#ifndef FUELGAUGE_H
#define FUELGAUGE_H

#include <Arduino.h>

/** @defgroup power_mode Power-source identifiers
 *  @{
 */
#define ACON  1  /**< AC mains or USB charger is supplying power */
#define BUBON 2  /**< Device is running on the backup battery (UPS mode) */
/** @} */

/**
 * @brief Initialise the MAX17048 over I²C and set the low-battery alert threshold.
 * @param Debug Non-zero to enable SparkFun library verbose debug output on Serial.
 */
void FGsetup(char Debug);

/**
 * @brief Poll the fuel gauge and update voltage, SoC, rate, and power-mode state.
 *
 * The power-mode state machine transitions to BUBON after 30 consecutive seconds
 * of discharge rate below –6 %/hr, and back to ACON after 30 s above –4.1 %/hr.
 *
 * Call from the main loop at every UPDATE_LOOP interval.
 */
void FGloop();

/** @brief Print current battery readings to the system log at DEBUG level. */
void FGDisplay();

/** @return Battery terminal voltage in volts. */
float GetCellV();

/** @return Battery state-of-charge in percent (0–100). */
float GetCellSoC();

/** @return @c true when the low-battery alert threshold (10 %) has triggered. */
bool GetCellAlert();

/** @return Non-zero if the MAX17048 failed to initialise. */
char GetFGerror();

/** @return Current power source: ACON (1) or BUBON (2). */
char GetPowerMode();

/** @return Instantaneous charge/discharge rate in %/hr (negative = discharging). */
float GetCellRate();

/** @return Battery voltage formatted to two decimal places, e.g. @c "3.82". */
String GetCellVString();

/**
 * @brief Battery SoC formatted to two decimal places.
 * @param Round When @c true, clamp the reported value to @c "100.00" at full charge.
 * @return SoC string, e.g. @c "75.23".
 */
String GetCellSoCString(bool Round);

/** @return Power-mode label: @c "AC", @c "BUB", or @c "NA". */
String GetPowerModeString();

/** @return Charge/discharge rate with one decimal place, e.g. @c "-2.3". */
String GetCellRateString();

#endif /* FUELGAUGE_H */
