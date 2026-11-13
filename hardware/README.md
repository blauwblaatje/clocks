# Hardware

Base board: Waveshare ESP32-C6-Touch-LCD-1.28, bare (not the -B housing).
512 KB RAM, 16 MB flash soldered on the board (the TF slot is a separate,
unused microSD slot). Comes with a 100 mm SH1.0 12-pin cable ending in
Dupont sockets.

Outline from the Waveshare drawing: R=18.75 mm round, 39.49 mm tall including
the USB-C tab, tab 9.92 mm wide between R2.00 fillets, 25.28 mm across the
shoulders, tab 7.48 mm long. Thickness not published; estimated in case/case.py.

Header pin use and all board pins: firmware/src/pins.h (from Waveshare's
repository github.com/waveshareteam/ESP32-C6-Touch-LCD-1.28, HARDWARE_REFERENCE.md
and the schematic). Identify wires by position in the plug, not by colour.
Header: GND, 3V3, RESET, BOOT, IO1, IO2, IO3, IO6, RXD (GPIO17), TXD (GPIO16),
I2C SCL (GPIO8), I2C SDA (GPIO18). IO6 is also the RTC's interrupt line.

Chip antenna: the ceramic part marked "C3" on the board edge (Waveshare's
dimension drawing). With USB at 3 o'clock it sits at about 1:36 seen from
the front, about 3 mm from the 2 o'clock switch in case v1.

Battery: 3.7 V LiPo up to about 8 x 30 x 40 mm (803040, 1000 mAh) with
protection circuit. The board socket is the 1.25 mm 2-pin type (MX1.25).
Cells sold with a PH2.0 plug need a 1.25 mm pigtail soldered on, one wire
at a time. Check polarity against the + and - printed at the socket.

Switches: 6x6x4.3 mm through-hole tactile, hand-wired to the header cable,
one shared ground wire. No PCB for the first unit.

Vibration motor (later version): 1027 coin motor, 3 V, on IO6 through a
through-hole transistor (2N2222 / BC337 / 2N3904 with 1 k base resistor, or
2N7000 with 10 k pull-down) and a diode across the motor.
