/**
 * @file IO.h
 * @brief Board GPIO, I²C, and UART0 initialisation plus debug LED control.
 */
#ifndef IO_H
#define IO_H

/**
 * @brief Configure GPIO directions, start UART0 at 115200, and start I²C on
 *        SDA=GPIO 27 / SCL=GPIO 26.
 */
void ConfigIO();

/**
 * @brief Drive the debug LED (GPIO 2) to a fixed state.
 * @param value @c 1 = on, @c 0 = off.
 */
void DebugLED(char value);

/** @brief Toggle the debug LED between on and off. */
void DebugLEDToggle();

#endif /* IO_H */
