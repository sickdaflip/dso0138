// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

/**
 * Proportional 2-bit-per-pixel font. Glyphs 0..94 are ASCII 32..126, the rest
 * are listed in `extra` (Unicode code points, ascending). Every glyph is
 * `height` rows tall and `widths[i]` columns wide (advance = bitmap width).
 * `digit` is the widest digit, used for tabular number layout.
 */
struct TextFont {
	const uint8_t *bits;
	const uint16_t *offsets;
	const uint8_t *widths;
	const uint16_t *extra;
	uint8_t height;
	uint8_t extraCount;
	uint8_t digit;
};
