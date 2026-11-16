// SPDX-License-Identifier: MIT
#include "gfx.h"
#include "tft.h"

namespace {

uint16_t nextCodepoint(const char *&s) {
	uint8_t c = uint8_t(*s);
	if (!c) return 0;
	s++;
	if (c < 0x80) return c;
	if ((c & 0xE0) == 0xC0) {
		uint8_t c2 = uint8_t(*s);
		if (!c2) return '?';
		s++;
		return uint16_t((c & 0x1F) << 6) | (c2 & 0x3F);
	}
	if ((c & 0xF0) == 0xE0) {
		uint8_t c2 = uint8_t(*s);
		if (!c2) return '?';
		s++;
		uint8_t c3 = uint8_t(*s);
		if (!c3) return '?';
		s++;
		return uint16_t((c & 0x0F) << 12) | uint16_t((c2 & 0x3F) << 6) | (c3 & 0x3F);
	}
	return '?';
}

int16_t glyphIndex(const TextFont &f, uint16_t cp) {
	if (cp >= 32 && cp < 127) return cp - 32;
	for (uint8_t i = 0; i < f.extraCount; i++) {
		if (f.extra[i] == cp) return 95 + i;
	}
	return '?' - 32;
}

void palette(uint16_t *pal, uint16_t fg, uint16_t bg) {
	pal[0] = bg;
	pal[1] = Gfx::blend(bg, fg, 85);
	pal[2] = Gfx::blend(bg, fg, 170);
	pal[3] = fg;
}

}  // namespace

namespace Gfx {

uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a) {
	uint16_t b = 255 - a;
	uint16_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * b) / 255;
	uint16_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * b) / 255;
	uint16_t bl = ((fg & 0x1F) * a + (bg & 0x1F) * b) / 255;
	return uint16_t((r << 11) | (g << 5) | bl);
}

int16_t textWidth(const TextFont &f, const char *s) {
	int16_t w = 0;
	for (;;) {
		uint16_t cp = nextCodepoint(s);
		if (!cp) break;
		w += f.widths[glyphIndex(f, cp)];
	}
	return w;
}

int16_t text(const TextFont &f, const char *s, int16_t x, int16_t y, uint16_t fg, uint16_t bg) {
	uint16_t pal[4];
	palette(pal, fg, bg);
	for (;;) {
		uint16_t cp = nextCodepoint(s);
		if (!cp) break;
		int16_t i = glyphIndex(f, cp);
		uint8_t w = f.widths[i];
		if (x + w > LCD_W) break;
		uint32_t pixels = uint32_t(w) * f.height;
		const uint8_t *p = f.bits + f.offsets[i];
		Tft::window(x, y, x + w - 1, y + f.height - 1);
		uint8_t byte = 0;
		for (uint32_t n = 0; n < pixels; n++) {
			if ((n & 3) == 0) byte = *p++;
			Tft::push(pal[(byte >> 6) & 3]);
			byte <<= 2;
		}
		Tft::end();
		x += w;
	}
	return x;
}

void textBox(const TextFont &f, const char *s, int16_t x, int16_t y, int16_t w, Align align,
             uint16_t fg, uint16_t bg) {
	int16_t tw = textWidth(f, s);
	if (tw > w) tw = w;
	int16_t sx = x;
	if (align == ALIGN_CENTER) sx = x + (w - tw) / 2;
	else if (align == ALIGN_RIGHT) sx = x + w - tw;
	if (sx > x) Tft::fillRect(x, y, sx - x, f.height, bg);
	int16_t ex = text(f, s, sx, y, fg, bg);
	if (ex < x + w) Tft::fillRect(ex, y, x + w - ex, f.height, bg);
}

void hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
	Tft::fillRect(x, y, w, 1, c);
}

void vline(int16_t x, int16_t y, int16_t h, uint16_t c) {
	Tft::fillRect(x, y, 1, h, c);
}

void pixel(int16_t x, int16_t y, uint16_t c) {
	if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
	Tft::window(x, y, x, y);
	Tft::push(c);
	Tft::end();
}

void roundRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fill, uint16_t outside) {
	// Radius 3 corner coverage per row (alpha of the first 3 pixels).
	static const uint8_t CORNER[3][3] = {
		{0, 90, 220},
		{90, 255, 255},
		{220, 255, 255},
	};
	Tft::fillRect(x, y + 3, w, h - 6, fill);
	for (uint8_t r = 0; r < 3; r++) {
		for (uint8_t side = 0; side < 2; side++) {
			int16_t yy = side ? y + h - 1 - r : y + r;
			Tft::window(x, yy, x + w - 1, yy);
			for (uint8_t i = 0; i < 3; i++) Tft::push(blend(outside, fill, CORNER[r][i]));
			Tft::pushN(fill, w - 6);
			for (int8_t i = 2; i >= 0; i--) Tft::push(blend(outside, fill, CORNER[r][i]));
			Tft::end();
		}
	}
}

int16_t badge(const TextFont &f, const char *s, int16_t x, int16_t y, int16_t minW,
              uint16_t fg, uint16_t fill, uint16_t outside) {
	int16_t w = textWidth(f, s) + 10;
	if (w < minW) w = minW;
	int16_t h = f.height + 2;
	roundRect(x, y, w, h, fill, outside);
	textBox(f, s, x + 3, y + 1, w - 6, ALIGN_CENTER, fg, fill);
	return w;
}

void rle4(const uint8_t *data, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t fg,
          uint16_t bg) {
	uint16_t pal[16];
	for (uint8_t i = 0; i < 16; i++) pal[i] = blend(bg, fg, uint8_t(i * 17));
	uint32_t left = uint32_t(w) * h;
	Tft::window(x, y, x + w - 1, y + h - 1);
	while (left) {
		uint8_t b = *data++;
		uint32_t run = (b & 15) + 1;
		if (run > left) run = left;
		Tft::pushN(pal[b >> 4], run);
		left -= run;
	}
	Tft::end();
}

void marker(int16_t x, int16_t y, uint8_t size, Dir d, uint16_t c, uint16_t bg) {
	// The marker occupies a (size+1) x (2*size+1) box (or transposed) whose
	// tip sits at (x, y).
	for (int16_t i = 0; i <= size; i++) {
		int16_t half = i;  // i pixels away from the tip
		switch (d) {
		case DIR_RIGHT:
			Tft::fillRect(x - i, y - size, 1, size - half, bg);
			Tft::fillRect(x - i, y - half, 1, 2 * half + 1, c);
			Tft::fillRect(x - i, y + half + 1, 1, size - half, bg);
			break;
		case DIR_LEFT:
			Tft::fillRect(x + i, y - size, 1, size - half, bg);
			Tft::fillRect(x + i, y - half, 1, 2 * half + 1, c);
			Tft::fillRect(x + i, y + half + 1, 1, size - half, bg);
			break;
		case DIR_DOWN:
			Tft::fillRect(x - size, y - i, size - half, 1, bg);
			Tft::fillRect(x - half, y - i, 2 * half + 1, 1, c);
			Tft::fillRect(x + half + 1, y - i, size - half, 1, bg);
			break;
		case DIR_UP:
			Tft::fillRect(x - size, y + i, size - half, 1, bg);
			Tft::fillRect(x - half, y + i, 2 * half + 1, 1, c);
			Tft::fillRect(x + half + 1, y + i, size - half, 1, bg);
			break;
		}
	}
}

}  // namespace Gfx
