#pragma once
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"

// Bit-bang MAX7219 8-digit 7-segment display driver (no-decode mode).
// Digit 0 is the leftmost (register address 1 on the chip).
class MAX7219Display {
public:
    static constexpr uint8_t SEG_BLANK = 0x00;
    static constexpr uint8_t SEG_DASH  = 0x01;  // middle segment only
    static constexpr uint8_t SEG_DP    = 0x80;  // decimal point bit

    MAX7219Display(gpio_num_t clk, gpio_num_t din, gpio_num_t load)
        : _clk(clk), _din(din), _load(load) {}

    void init() {
        gpio_reset_pin(_clk);
        gpio_reset_pin(_din);
        gpio_reset_pin(_load);
        gpio_set_direction(_clk,  GPIO_MODE_OUTPUT);
        gpio_set_direction(_din,  GPIO_MODE_OUTPUT);
        gpio_set_direction(_load, GPIO_MODE_OUTPUT);
        gpio_set_level(_clk,  0);
        gpio_set_level(_din,  0);
        gpio_set_level(_load, 1);

        _send(REG_SHUTDOWN,    0x01);  // normal operation
        _send(REG_DISPLAYTEST, 0x00);
        _send(REG_SCANLIMIT,   0x07);  // all 8 digits
        _send(REG_DECODE,      0x00);  // no BCD decode — raw segments
        setIntensity(8);
        clear();
    }

    void setIntensity(uint8_t level) { _send(REG_INTENSITY, level & 0x0F); }

    // Write raw segment bytes for all 8 digits, left to right.
    void setSegments(const uint8_t segs[8]) {
        for (int i = 0; i < 8; i++) _send(8 - i, segs[i]);
    }

    void clear() { for (int i = 1; i <= 8; i++) _send(i, 0x00); }

    static uint8_t digit(int d) { return DIGITS[d % 10]; }

private:
    gpio_num_t _clk, _din, _load;

    static constexpr uint8_t REG_DECODE      = 0x09;
    static constexpr uint8_t REG_INTENSITY   = 0x0A;
    static constexpr uint8_t REG_SCANLIMIT   = 0x0B;
    static constexpr uint8_t REG_SHUTDOWN    = 0x0C;
    static constexpr uint8_t REG_DISPLAYTEST = 0x0F;

    static constexpr uint8_t DIGITS[10] = {
        0x7E, 0x30, 0x6D, 0x79, 0x33,  // 0-4
        0x5B, 0x5F, 0x70, 0x7F, 0x7B   // 5-9
    };

    void _delay() { esp_rom_delay_us(1); }

    void _send(uint8_t reg, uint8_t data) {
        gpio_set_level(_load, 0);
        _shiftOut(reg);
        _shiftOut(data);
        gpio_set_level(_load, 1);
        _delay();
    }

    void _shiftOut(uint8_t b) {
        for (int i = 7; i >= 0; i--) {
            gpio_set_level(_clk, 0);
            gpio_set_level(_din, (b >> i) & 1);
            _delay();
            gpio_set_level(_clk, 1);
            _delay();
        }
    }
};
