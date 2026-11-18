// Waveshare ESP32-C6-Touch-LCD-1.28 pins. Source: Waveshare's repository
// github.com/waveshareteam/ESP32-C6-Touch-LCD-1.28 (commit d73e23c),
// HARDWARE_REFERENCE.md, the BSP header and the schematic (read 2026-11-10).
#pragma once

// ---- our buttons, on the 12-pin expansion header ----
// GPIO0-7 are the ESP32-C6 low-power pins that can wake from deep sleep.
#define PIN_BTN_CROWN 1
#define PIN_BTN_TIMEOUT 2 // 2 o'clock
#define PIN_BTN_UNDO 3    // 10 o'clock
// IO6 is reserved for a vibration motor (via transistor + flyback diode).
// NOTE: on the board IO6 is also the PCF85063 RTC's interrupt line. The
// firmware must never enable RTC interrupts while a motor is on IO6.
#define PIN_VIBRA 6

// ---- on the board ----
// Display GC9A01, SPI (shared with the TF card, which we do not use)
#define PIN_LCD_SCLK 11
#define PIN_LCD_MOSI 21
#define PIN_LCD_MISO 22 // not needed for the display
#define PIN_LCD_CS 10
#define PIN_LCD_DC 19
#define PIN_LCD_RST 20
#define PIN_LCD_BL 7 // backlight, PWM
// Touch CST816S, I2C address 0x15 (bus shared with the QMI8658 IMU 0x6B and
// the PCF85063 RTC 0x51)
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 8
#define PIN_TP_INT 4 // shared with the IMU's INT2
#define PIN_TP_RST 23 // also the charger's BC_State signal: do not use otherwise
#define TP_I2C_ADDR 0x15
// Battery: BAT through 200k/100k to the ADC, so V_bat = 3 x V_adc
#define PIN_BAT_ADC 0
#define BAT_DIVIDER 3.0f
// The charger has no status GPIO of its own (only the CHG LED), so the
// firmware cannot tell when it is charging.
#define PIN_BOOT 9 // BOOT button, strapping pin
// UART0 on the header: TXD 16, RXD 17. USB D-/D+: 12/13.
