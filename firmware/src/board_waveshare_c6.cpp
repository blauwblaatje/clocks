// Waveshare ESP32-C6-Touch-LCD-1.28: GC9A01 display (through Arduino_GFX),
// CST816S touch, backlight PWM, battery on the ADC, three switches on the
// header. Compiles; not run on the board yet.
#include "board.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <driver/usb_serial_jtag.h>
#include <esp_sleep.h>

#include "pins_waveshare_c6.h"

namespace board {

const upd::Board& kBoard = upd::kWaveshareC6;
__attribute__((used)) const char kMarker[] = CLOCK_BOARD_MARKER_PREFIX "waveshare-c6;";
const int kSize = 240;

// The board sits in the case a quarter turn round (USB at 3 o'clock seen from
// the front, the board's own "down" is its USB side). Picture and touch are
// turned by the same amount. If the picture comes out upside down on the
// first run, change kRotation from 1 to 3; touch follows automatically.
static constexpr uint8_t kRotation = 1;

static const int kW = 240, kH = 240;
static const int kBufLines = 24; // 240 x 24 x 2 bytes x 2 = 23 KB, not a full frame
static const uint32_t kBacklightHz = 5000;
static const int kBacklightMaxDuty = 860; // of 1023: Waveshare's BSP caps the backlight at 84 %

static Arduino_HWSPI bus(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, -1);
// GC9A01 with IPS colour inversion, as in Waveshare's Arduino_GFX example.
static Arduino_GC9A01 lcd(&bus, PIN_LCD_RST, kRotation, true, kW, kH);

// Touch coordinates from the panel's own frame to the screen's (rotation).
static void rotateTouch(uint8_t r, int nx, int ny, int& lx, int& ly) {
    switch (r & 3) {
    case 0: lx = nx, ly = ny; break;
    case 1: lx = ny, ly = kW - 1 - nx; break;
    case 2: lx = kW - 1 - nx, ly = kH - 1 - ny; break;
    default: lx = kH - 1 - ny, ly = nx; break;
    }
}

void begin() {
    for (int pin : {PIN_BTN_CROWN, PIN_BTN_UNDO, PIN_BTN_TIMEOUT}) pinMode(pin, INPUT_PULLUP);
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

uint8_t buttons() {
    // Crown at 12, undo at 10 (left), timeout/pause at 2 (right), seen from the front.
    return (digitalRead(PIN_BTN_CROWN) == LOW ? kCrown : 0) | (digitalRead(PIN_BTN_UNDO) == LOW ? kLeft : 0) |
           (digitalRead(PIN_BTN_TIMEOUT) == LOW ? kRight : 0);
}

bool battery(int& mv, int& pct, bool& charging) {
    charging = false; // the board has no charge-status pin (only the CHG LED)
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
    mv = (int)(sum / 8 * BAT_DIVIDER);
    if (mv < 2500 || mv > 4500) return false; // no battery, or not measured right
    pct = lipoPercent(mv);
    return true;
}

// A computer on the USB port: the board has no pin for a plain charger.
bool usbPower() { return usb_serial_jtag_is_connected(); }

void sleep() {
    // Wake on the crown (IO1 is a low-power GPIO, so ext1 can wake on it).
    // TODO: require the 1 s hold after waking (check on wake, else sleep again).
    displayOff();
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_CROWN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
    for (;;) {
    }
}

std::vector<std::string> info() {
    int mv = 0, pct = 0;
    bool charging = false;
    if (!battery(mv, pct, charging)) return {"Battery: no reading"};
    return {"Battery: " + std::to_string(pct) + " % (" + std::to_string(mv) + " mV)"};
}

} // namespace board
