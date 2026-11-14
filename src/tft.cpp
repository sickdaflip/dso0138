// SPDX-License-Identifier: MIT
#include "tft.h"
#include "hal.h"

namespace {

inline void csLow() { TFT_CS_PORT->BRR = 1u << TFT_CS_PIN; }
inline void csHigh() { TFT_CS_PORT->BSRR = 1u << TFT_CS_PIN; }
inline void rsCmd() { TFT_RS_PORT->BRR = 1u << TFT_RS_PIN; }
inline void rsData() { TFT_RS_PORT->BSRR = 1u << TFT_RS_PIN; }

void command(uint8_t c) {
	rsCmd();
	Tft::write8(c);
	rsData();
}

void data(uint8_t d) {
	Tft::write8(d);
}

void data16(uint16_t d) {
	Tft::write8(d >> 8);
	Tft::write8(d & 0xFF);
}

/** Init table: command, argument count (bit 7: 120 ms delay after), args. */
const uint8_t INIT[] = {
	0x01, 0x80,                         // software reset
	0x28, 0,                            // display off
	0xC0, 1, 0x23,                      // power control 1
	0xC1, 1, 0x10,                      // power control 2
	0xC5, 2, 0x2B, 0x2B,                // VCOM control 1
	0xC7, 1, 0xC0,                      // VCOM control 2
	0x36, 1, TFT_MADCTL,                // memory access control
	0x3A, 1, 0x55,                      // 16 bit per pixel
	0xB1, 2, 0x00, 0x1B,                // frame rate 70 Hz
	0xB7, 1, 0x07,                      // entry mode
	0x11, 0x80,                         // sleep out
	0x29, 0,                            // display on
	0x00
};

}  // namespace

namespace Tft {

void begin() {
	using namespace hal;
	for (uint8_t p = 0; p < 8; p++) pinMode(GPIOB, p, PIN_OUTPUT);
	pinMode(TFT_RD_PORT, TFT_RD_PIN, PIN_OUTPUT);
	pinMode(TFT_RST_PORT, TFT_RST_PIN, PIN_OUTPUT);
	pinMode(TFT_CS_PORT, TFT_CS_PIN, PIN_OUTPUT_2MHZ);
	pinMode(TFT_RS_PORT, TFT_RS_PIN, PIN_OUTPUT_2MHZ);
	pinMode(TFT_WR_PORT, TFT_WR_PIN, PIN_OUTPUT_2MHZ);
	pinHigh(TFT_RD_PORT, TFT_RD_PIN);
	pinHigh(TFT_WR_PORT, TFT_WR_PIN);
	csHigh();

	pinLow(TFT_RST_PORT, TFT_RST_PIN);
	delayMs(10);
	pinHigh(TFT_RST_PORT, TFT_RST_PIN);
	delayMs(120);

	csLow();
	const uint8_t *p = INIT;
	for (;;) {
		uint8_t cmd = *p++;
		if (!cmd) break;
		uint8_t n = *p++;
		bool wait = n & 0x80;
		n &= 0x7F;
		command(cmd);
		while (n--) data(*p++);
		if (wait) delayMs(120);
	}
	csHigh();
}

void window(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	csLow();
	command(0x2A);
	data16(x0);
	data16(x1);
	command(0x2B);
	data16(y0);
	data16(y1);
	command(0x2C);
}

void end() {
	csHigh();
}

void pushN(uint16_t c, uint32_t n) {
	uint8_t hi = c >> 8, lo = c & 0xFF;
	if (hi == lo) {
		// Same byte twice: set the bus once and only toggle WR.
		GPIOB->BSRR = (0xFFu << 16) | hi;
		n *= 2;
		while (n--) {
			GPIOC->BRR = 1u << TFT_WR_PIN;
			GPIOC->BSRR = 1u << TFT_WR_PIN;
		}
		return;
	}
	while (n--) {
		write8(hi);
		write8(lo);
	}
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > LCD_W) w = LCD_W - x;
	if (y + h > LCD_H) h = LCD_H - y;
	if (w <= 0 || h <= 0) return;
	window(x, y, x + w - 1, y + h - 1);
	pushN(c, uint32_t(w) * h);
	end();
}

void fillScreen(uint16_t c) {
	fillRect(0, 0, LCD_W, LCD_H, c);
}

}  // namespace Tft
