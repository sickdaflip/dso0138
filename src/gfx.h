// SPDX-License-Identifier: MIT
/**
 * Drawing primitives. Text calls paint their full bounding box (foreground
 * and background), so a redraw never needs a prior clear.
 */
#pragma once
#include <stdint.h>
#include "fontdefs.h"

enum Align : uint8_t { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

namespace Gfx {

uint16_t blend(uint16_t bg, uint16_t fg, uint8_t alpha);  // alpha 0..255

/** UTF-8 text. Returns the x position after the last glyph. */
int16_t text(const TextFont &f, const char *s, int16_t x, int16_t y, uint16_t fg, uint16_t bg);
int16_t textWidth(const TextFont &f, const char *s);

/** Paints a box of width `w` with the text aligned inside it. */
void textBox(const TextFont &f, const char *s, int16_t x, int16_t y, int16_t w, Align align,
             uint16_t fg, uint16_t bg);

void hline(int16_t x, int16_t y, int16_t w, uint16_t c);
void vline(int16_t x, int16_t y, int16_t h, uint16_t c);
void pixel(int16_t x, int16_t y, uint16_t c);

/** Rounded rectangle (radius 3) with corners blended against `outside`. */
void roundRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t outside);

/** Pill badge with centered text; returns its width. */
int16_t badge(const TextFont &f, const char *s, int16_t x, int16_t y, int16_t minW,
              uint16_t fg, uint16_t fill, uint16_t outside);

/**
 * Draws a 4-bit alpha run-length encoded mask (see tools/gen_logo.py) in
 * `fg` over `bg`.
 */
void rle4(const uint8_t *data, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fg,
          uint16_t bg);

/** Solid triangle marker pointing left/right/up/down. */
enum Dir : uint8_t { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };
void marker(int16_t x, int16_t y, uint8_t size, Dir d, uint16_t c, uint16_t bg);

}  // namespace Gfx
