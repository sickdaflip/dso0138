// SPDX-License-Identifier: MIT
/**
 * ILI9341 driver for the 8-bit 8080 parallel bus of the DSO138.
 * Landscape 320 x 240. Same streaming interface as the SPI driver of the
 * Maiskolben firmware, so the drawing code can be shared.
 */
#pragma once
#include <stdint.h>
#include "board.h"

#ifndef TFT_MADCTL
#define TFT_MADCTL 0xE8  // MY | MX | MV | BGR: landscape, stock orientation
#endif

namespace Tft {

void begin();

/** Opens a drawing window; follow with push()/pushN() and end(). */
void window(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void end();

#ifdef DSO_SIM
void push(uint16_t c);
#else
// D0..D7 on PB0..PB7; WR strobe on PC15.
static inline void write8(uint8_t v) {
	GPIOB->BSRR = (0xFFu << 16) | v;  // set wins over reset for the same bit
	GPIOC->BRR = 1u << TFT_WR_PIN;
	GPIOC->BSRR = 1u << TFT_WR_PIN;
}

static inline void push(uint16_t c) {
	write8(c >> 8);
	write8(c & 0xFF);
}
#endif

void pushN(uint16_t c, uint32_t n);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
void fillScreen(uint16_t c);

}  // namespace Tft
