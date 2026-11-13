#include "board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include "pins.h"

namespace board {

static const int kW = 240, kH = 240;
static const int kBufLines = 24; // 240 x 24 x 2 bytes x 2 = 23 KB, not a full frame
static const uint32_t kBacklightHz = 5000;
static const int kBacklightMaxDuty = 860; // of 1023: Waveshare's BSP caps the backlight at 84 %

static Arduino_HWSPI bus(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, -1);
// GC9A01 with IPS colour inversion, as in Waveshare's Arduino_GFX example.
static Arduino_GC9A01 lcd(&bus, PIN_LCD_RST, kRotation, true, kW, kH);

void rotateTouch(uint8_t r, int nx, int ny, int& lx, int& ly) {
    switch (r & 3) {
    case 0: lx = nx, ly = ny; break;
    case 1: lx = ny, ly = kW - 1 - nx; break;
    case 2: lx = kW - 1 - nx, ly = kH - 1 - ny; break;
    default: lx = kH - 1 - ny, ly = nx; break;
    }
}

void begin() {
    ledcAttach(PIN_LCD_BL, kBacklightHz, 10);
    ledcWrite(PIN_LCD_BL, 0); // dark until the first frame
    lcd.begin();
    lcd.fillScreen(0x0000);

    // Touch: reset pulse, then I2C (CST816S, address 0x15).
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);
    delay(10);
    digitalWrite(PIN_TP_RST, HIGH);
    delay(50);
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
}

void setBrightness(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ledcWrite(PIN_LCD_BL, (uint32_t)(kBacklightMaxDuty * pct / 100));
}

void displayOff() {
    ledcWrite(PIN_LCD_BL, 0);
    lcd.displayOff();
}

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    lcd.draw16bitRGBBitmap(a->x1, a->y1, (uint16_t*)px, w, h);
    lv_display_flush_ready(d);
}

lv_display_t* createDisplay() {
    static uint8_t buf1[kW * kBufLines * 2], buf2[kW * kBufLines * 2];
    lv_display_t* d = lv_display_create(kW, kH);
    lv_display_set_buffers(d, buf1, buf2, sizeof buf1, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    return d;
}

// CST816S: registers 0x01 gesture, 0x02 number of touch points, 0x03/0x04
// X high/low, 0x05/0x06 Y high/low (12-bit coordinates).
static bool readTouch(int& x, int& y) {
    Wire.beginTransmission(TP_I2C_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TP_I2C_ADDR, (uint8_t)5) != 5) return false;
    uint8_t n = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if ((n & 0x0F) == 0) return false;
    int nx = ((xh & 0x0F) << 8) | xl, ny = ((yh & 0x0F) << 8) | yl;
    if (nx >= kW || ny >= kH) return false;
    rotateTouch(kRotation, nx, ny, x, y);
    return true;
}

static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
    static int lastX = 0, lastY = 0;
    int x, y;
    // INT is low while a finger is down; skip the I2C read otherwise
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

// Single-cell LiPo at light load: rough voltage-to-charge table.
static int lipoPercent(int mv) {
    static const int table[][2] = {{4180, 100}, {4100, 90}, {4000, 80}, {3920, 70}, {3850, 60}, {3800, 50},
                                   {3750, 40}, {3710, 30}, {3670, 20}, {3610, 10}, {3500, 5},  {3300, 0}};
    if (mv >= table[0][0]) return 100;
    for (size_t i = 1; i < sizeof table / sizeof table[0]; i++) {
        if (mv >= table[i][0]) {
            int v0 = table[i][0], v1 = table[i - 1][0], p0 = table[i][1], p1 = table[i - 1][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

bool battery(int& mv, int& pct) {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
    mv = (int)(sum / 8 * BAT_DIVIDER);
    if (mv < 2500 || mv > 4500) return false; // no battery, or not measured right
    pct = lipoPercent(mv);
    return true;
}

} // namespace board
