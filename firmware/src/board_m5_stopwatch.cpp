// M5Stack StopWatch: CO5300 AMOLED over QSPI (through Arduino_GFX), CST820
// touch, M5PM1 power chip (power button, battery, power off), M5IOE1 IO
// expander (display and touch power and reset). Compiles; not run on the
// board yet. Pins and sources: pins_m5_stopwatch.h.
#include "board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <M5PM1.h>
#include <Wire.h>
#include <esp_sleep.h>

#include "pins_m5_stopwatch.h"

namespace board {

const upd::Board& kBoard = upd::kStopWatch;
__attribute__((used)) const char kMarker[] = CLOCK_BOARD_MARKER_PREFIX "m5-stopwatch;";
const int kSize = 466;

// Which physical button does what. Not decided yet (Rogier, 2026-11-18:
// "decide when I have one"): for now the power button is the crown and the
// yellow and blue buttons are the 10 and 2 o'clock buttons, as on the
// Waveshare. Set kCrownIsPowerButton to false to make the blue button the
// crown and the power button the 2 o'clock button instead.
static constexpr bool kCrownIsPowerButton = true;

static const int kW = 466, kH = 466;
static const int kBufLines = 40; // 466 x 40 x 2 bytes x 2 = 75 KB internal RAM, not a full frame

static Arduino_ESP32QSPI bus(PIN_OLED_CS, PIN_OLED_SCK, PIN_OLED_D0, PIN_OLED_D1, PIN_OLED_D2, PIN_OLED_D3);
static Arduino_CO5300 lcd(&bus, GFX_NOT_DEFINED /* reset through the expander */, 0, kW, kH, OLED_COL_OFFSET, 0,
                          OLED_COL_OFFSET, 0);
static M5PM1 pm1;
static bool pmOk = false;

// ---- I2C helpers for the expander and the power chip's rail register ----

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t& v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
    v = Wire.read();
    return true;
}
static void bits(uint8_t addr, uint8_t reg, uint8_t mask, bool on) {
    uint8_t v = 0;
    if (!i2cRead(addr, reg, v)) return;
    i2cWrite(addr, reg, on ? (v | mask) : (v & ~mask));
}
static bool answers(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// M5IOE1 registers (M5IOE1 library 1.0.9)
enum : uint8_t { kModeL = 0x03, kModeH = 0x04, kOutL = 0x05, kOutH = 0x06, kDrvL = 0x13, kDrvH = 0x14 };
enum : uint8_t { kP1 = 0x01, kP3 = 0x04, kP4 = 0x08, kP5 = 0x10, kP8 = 0x80, kP10 = 0x02 /* in the _H registers */ };

// Power-on as M5GFX does it for this board (pmic_ops.hpp, stopwatch_power_on).
static void powerOn() {
    // M5PM1: no I2C sleep, no watchdog, 3.3 V and 5 V rails on.
    i2cWrite(PM1_I2C_ADDR, 0x09, 0x00);
    i2cWrite(PM1_I2C_ADDR, 0x0A, 0x00);
    bits(PM1_I2C_ADDR, 0x06, 0x17, true);
    pinMode(PIN_OLED_CS, OUTPUT);
    digitalWrite(PIN_OLED_CS, HIGH);
    // The expander starts later than the power chip.
    for (uint32_t t0 = millis(); !answers(IOE1_I2C_ADDR) && millis() - t0 < 500;) delay(5);
    const uint8_t outs = kP1 | kP3 | kP4 | kP5 | kP8;
    i2cWrite(IOE1_I2C_ADDR, 0x23, 0x00);
    bits(IOE1_I2C_ADDR, kDrvL, outs, false); // push-pull
    bits(IOE1_I2C_ADDR, kModeL, outs, true); // outputs
    bits(IOE1_I2C_ADDR, kOutL, kP1 | kP4 | kP5 | kP8, true); // display power on, audio off
    delay(10);
    bits(IOE1_I2C_ADDR, kOutL, kP4 | kP5, false); // touch and display reset
    delay(8);
    bits(IOE1_I2C_ADDR, kOutL, kP4 | kP5, true);
    delay(2);
    bits(IOE1_I2C_ADDR, kDrvH, kP10, false); // speaker amplifier: output, off
    bits(IOE1_I2C_ADDR, kModeH, kP10, true);
    bits(IOE1_I2C_ADDR, kOutH, kP10, false);
}

void begin() {
    pinMode(PIN_KEY_A, INPUT_PULLUP);
    pinMode(PIN_KEY_B, INPUT_PULLUP);
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    pinMode(PIN_OLED_TE, INPUT_PULLUP);
    Wire.begin(PIN_SYS_SDA, PIN_SYS_SCL, 100000); // the power chip runs at 100 kHz
    powerOn();
    pmOk = pm1.begin(&Wire, PM1_I2C_ADDR, PIN_SYS_SDA, PIN_SYS_SCL, M5PM1_I2C_FREQ_100K) == M5PM1_OK;
    if (pmOk) {
        pm1.setI2cSleepTime(0);
        pm1.wdtSet(0);
        pm1.setChargeEnable(true);
        pm1.gpioSetFunc((m5pm1_gpio_num_t)PM1_PIN_CHG_STAT, M5PM1_GPIO_FUNC_GPIO);
        pm1.gpioSetMode((m5pm1_gpio_num_t)PM1_PIN_CHG_STAT, M5PM1_GPIO_MODE_INPUT);
        // The power button is a game button in either mapping: a click must
        // not reset the board and a quick double press must not switch it
        // off. Power off is menu > Power > Sleep; a 4 s hold is left to the
        // chip (what it does then is to be checked on the board).
        pm1.setSingleResetDisable(true);
        pm1.setDoubleOffDisable(true);
        pm1.btnSetConfig(M5PM1_BTN_TYPE_LONG, M5PM1_BTN_LONG_PRESS_DELAY_4000MS);
    }
    lcd.begin(80000000);
    lcd.fillScreen(0x0000);
    lcd.setBrightness(0); // dark until the first frame
    if (!answers(TP_I2C_ADDR) && answers(TP_CST9217_ADDR))
        Serial.println("touch: CST9217 found, not supported yet (touch stays off)");
}

void setBrightness(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lcd.setBrightness((uint8_t)(255 * pct / 100));
}

void displayOff() {
    lcd.setBrightness(0);
    lcd.displayOff();
    bits(IOE1_I2C_ADDR, kOutL, kP8, false); // display power
}

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    lcd.draw16bitRGBBitmap(a->x1, a->y1, (uint16_t*)px, w, h);
    lv_display_flush_ready(d);
}

// The CO5300 takes only areas that start on an even column and row and are
// an even number of pixels wide and high.
static void roundArea(lv_event_t* e) {
    lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

lv_display_t* createDisplay() {
    static uint8_t buf1[kW * kBufLines * 2], buf2[kW * kBufLines * 2];
    lv_display_t* d = lv_display_create(kW, kH);
    lv_display_set_buffers(d, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    lv_display_add_event_cb(d, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
    return d;
}

// CST820, CST816S protocol: 0x02 number of points, 0x03/0x04 X, 0x05/0x06 Y.
static bool readTouch(int& x, int& y) {
    Wire.beginTransmission(TP_I2C_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TP_I2C_ADDR, (uint8_t)5) != 5) return false;
    uint8_t n = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if ((n & 0x0F) == 0) return false;
    int nx = ((xh & 0x0F) << 8) | xl, ny = ((yh & 0x0F) << 8) | yl;
    if (nx >= TP_RANGE || ny >= TP_RANGE) return false;
    x = nx * kW / TP_RANGE;
    y = ny * kH / TP_RANGE;
    return true;
}

static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
    static int lastX = 0, lastY = 0;
    int x, y;
    if (digitalRead(PIN_TP_INT) == LOW && readTouch(x, y)) {
        lastX = x;
        lastY = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = lastX;
    data->point.y = lastY;
}

lv_indev_t* createTouch() {
    lv_indev_t* t = lv_indev_create();
    lv_indev_set_type(t, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(t, touchRead);
    return t;
}

// The power button is read over I2C, so not on every loop: every 10 ms.
static bool powerButton() {
    static uint32_t at = 0;
    static bool down = false;
    uint32_t now = millis();
    if (pmOk && now - at >= 10) {
        at = now;
        bool p = false;
        if (pm1.btnGetState(&p) == M5PM1_OK) down = p;
    }
    return down;
}

uint8_t buttons() {
    bool yellow = digitalRead(PIN_KEY_A) == LOW, blue = digitalRead(PIN_KEY_B) == LOW, power = powerButton();
    if (kCrownIsPowerButton)
        return (power ? kCrown : 0) | (yellow ? kLeft : 0) | (blue ? kRight : 0);
    return (blue ? kCrown : 0) | (yellow ? kLeft : 0) | (power ? kRight : 0);
}

static bool externalPower() {
    uint16_t vin = 0;
    return pmOk && pm1.readVin(&vin) == M5PM1_OK && vin > 4000;
}

bool battery(int& mv, int& pct, bool& charging) {
    uint16_t v = 0;
    charging = false;
    if (!pmOk || pm1.readVbat(&v) != M5PM1_OK) return false;
    mv = v;
    if (mv < 2500 || mv > 4500) return false;
    pct = lipoPercent(mv);
    uint8_t chg = 1;
    charging = externalPower() && pm1.gpioGetInput((m5pm1_gpio_num_t)PM1_PIN_CHG_STAT, &chg) == M5PM1_OK && chg == 0;
    return true;
}

// The power chip sees 5 V on the USB port: a computer or a plain charger.
bool usbPower() { return externalPower(); }

// Power off through the power chip; the power button switches it on again
// (a fresh start: settings are in NVS).
void sleep() {
    displayOff();
    if (pmOk) pm1.shutdown();
    delay(500);
    // Still running (no power chip, or on USB the chip keeps the board on):
    // deep sleep until the yellow button.
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_KEY_A, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
    for (;;) {
    }
}

std::vector<std::string> info() {
    std::vector<std::string> l;
    int mv = 0, pct = 0;
    bool charging = false;
    if (battery(mv, pct, charging))
        l.push_back("Battery: " + std::to_string(pct) + " % (" + std::to_string(mv) + " mV)" +
                    (charging ? ", charging" : ""));
    else l.push_back(pmOk ? "Battery: no reading" : "Battery: power chip not found");
    if (externalPower()) l.push_back("Power: USB");
    return l;
}

} // namespace board
