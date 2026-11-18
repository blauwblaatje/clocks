// M5Stack StopWatch (SKU C152, ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM).
// Sources, read 2026-11-18: docs.m5stack.com/en/core/StopWatch (pin map),
// M5GFX board data and power-on sequence (board_detect/m5: esp32s3_board_data.hpp,
// pmic_ops.hpp "stopwatch_power_on"), M5Unified (buttons), M5PM1 1.0.7
// (power chip registers) and M5StopWatch-UserDemo (hal_pmic.cpp, hal_display.cpp).
#pragma once

// ---- buttons ----
// Two buttons on GPIOs, low while pressed (M5Unified reads them inverted);
// on the front, yellow upper left and blue upper right.
#define PIN_KEY_A 2 // yellow
#define PIN_KEY_B 1 // blue
// The third button is the power button. The M5PM1 power chip reads it; the
// firmware asks over I2C (register 0x48 bit 0).

// ---- display: CO5300 AMOLED, 466 x 466, QSPI ----
#define PIN_OLED_CS 39
#define PIN_OLED_SCK 40
#define PIN_OLED_D0 41
#define PIN_OLED_D1 42
#define PIN_OLED_D2 46
#define PIN_OLED_D3 45
#define PIN_OLED_TE 38
// Reset and power through the M5IOE1 expander (below). The panel's memory is
// wider than the glass: 6 columns offset (M5GFX: offset_x 6).
#define OLED_COL_OFFSET 6

// ---- internal I2C bus (touch, power chip, expander, RTC, IMU, audio codec) ----
#define PIN_SYS_SDA 47
#define PIN_SYS_SCL 48
#define PIN_TP_INT 13
#define PIN_PM_IRQ 12 // M5PM1 interrupt (not used)
// Touch: CST820 at 0x15 (CST816S protocol) reporting 0..233 per axis, half
// the display (M5GFX: touch range 0..233). Some units have a CST9217 at 0x5A
// instead (M5GFX probes for it); that one is not supported here yet.
#define TP_I2C_ADDR 0x15
#define TP_CST9217_ADDR 0x5A
#define TP_RANGE 234
// M5PM1 power chip
#define PM1_I2C_ADDR 0x6E
#define PM1_PIN_CHG_STAT 2 // PM1 GPIO2: low while charging
// M5IOE1 IO expander. Its pins P1..P14: bit n-1 of the _L registers (P1-P8)
// or bit n-9 of the _H registers (P9-P14).
#define IOE1_I2C_ADDR 0x4F
//   P1  rear-bus mux (high = USB)        P3  audio power
//   P4  touch reset (low = reset)        P5  display reset (low = reset)
//   P8  display power                    P9  vibration motor PWM (not used yet)
//   P10 speaker amplifier enable
