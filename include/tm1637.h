#pragma once
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"

// Bit-bang TM1637 4-digit 7-segment display driver.
class TM1637Display {
public:
    static constexpr uint8_t SEG_G = 0x40;  // middle segment — displays as a dash

    TM1637Display(gpio_num_t clk, gpio_num_t dio) : _clk(clk), _dio(dio) {}

    void init() {
        gpio_reset_pin(_clk);
        gpio_reset_pin(_dio);
        gpio_set_direction(_clk, GPIO_MODE_OUTPUT);
        gpio_set_direction(_dio, GPIO_MODE_OUTPUT);
        gpio_set_level(_clk, 1);
        gpio_set_level(_dio, 1);
    }

    void setBrightness(uint8_t b) { _brightness = b & 0x07; }

    void setSegments(const uint8_t segs[4]) {
        _write(0x40);          // auto-increment address mode
        _start();
        _writeByte(0xC0);      // starting at address 0
        for (int i = 0; i < 4; i++) _writeByte(segs[i]);
        _stop();
        _write(0x88 | _brightness);  // display on + brightness
    }

    // Display a number with optional decimal points.
    // dots bitmask: 0x80=after digit 0, 0x40=after digit 1, 0x20=after digit 2, 0x10=after digit 3
    // For XXX.X format pass dots=0x20, leading_zero=false, num = bpm*10 rounded.
    void showNumberDecEx(int num, uint8_t dots, bool leading_zero,
                         int length = 4, int pos = 0) {
        uint8_t segs[4] = {0, 0, 0, 0};
        if (num < 0) num = -num;

        for (int i = length - 1; i >= 0; i--) {
            bool hasDigit = (num > 0) || (i == length - 1);
            segs[i + pos] = hasDigit ? DIGITS[num % 10] : (leading_zero ? DIGITS[0] : 0);
            num /= 10;
        }

        if (dots & 0x80) segs[0] |= 0x80;
        if (dots & 0x40) segs[1] |= 0x80;
        if (dots & 0x20) segs[2] |= 0x80;
        if (dots & 0x10) segs[3] |= 0x80;

        setSegments(segs);
    }

private:
    gpio_num_t _clk, _dio;
    uint8_t    _brightness = 7;

    static constexpr uint8_t DIGITS[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,  // 0-4
        0x6D, 0x7D, 0x07, 0x7F, 0x6F   // 5-9
    };

    void _delay()   { esp_rom_delay_us(2); }
    void _clkHi()   { gpio_set_level(_clk, 1); _delay(); }
    void _clkLo()   { gpio_set_level(_clk, 0); _delay(); }
    void _dioHi()   { gpio_set_level(_dio, 1); _delay(); }
    void _dioLo()   { gpio_set_level(_dio, 0); _delay(); }

    void _start()   { _dioHi(); _clkHi(); _dioLo(); _clkLo(); }
    void _stop()    { _dioLo(); _clkHi(); _dioHi(); }

    void _writeByte(uint8_t b) {
        for (int i = 0; i < 8; i++) {
            _clkLo();
            if (b & 0x01) _dioHi(); else _dioLo();
            _clkHi();
            b >>= 1;
        }
        // ACK clock pulse — TM1637 pulls DIO low; we ignore it
        _clkLo(); _dioHi(); _clkHi(); _clkLo();
    }

    void _write(uint8_t cmd) { _start(); _writeByte(cmd); _stop(); }
};
