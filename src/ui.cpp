// SPDX-License-Identifier: MIT
#include "ui.h"
#include <string.h>
#include "board.h"
#include "colors.h"
#include "fonts.h"
#include "gfx.h"
#include "i18n.h"
#include "tft.h"

namespace {

constexpr int16_t STATUS_Y = 1;
constexpr int16_t BAR_Y = 221;
constexpr int16_t BAR_H = LCD_H - BAR_Y;
constexpr int16_t MID_X = GRID_W / 2;
constexpr int16_t MID_Y = GRID_H / 2;

// Last drawn trace span per column (grid-relative rows), -1 = nothing
int16_t oldTop[GRID_W];
int16_t oldBot[GRID_W];
int16_t oldZeroY = -1, oldLevelY = -1, oldTrigX = -1000;

// Overlay panels are drawn once, then only their contents update.
int16_t menuH = 0;
bool zeroDrawn = false;

// Status chip cache, so unchanged chips are not repainted
struct Chip {
	int16_t x, w;
	char text[24];
	uint8_t style;
};
enum ChipId : uint8_t { CH_RUN, CH_TB, CH_VDIV, CH_MODE, CH_EDGE, CH_LEVEL, CH_COUNT };
Chip chips[CH_COUNT] = {
	{1, 42, "", 0xFF},
	{45, 56, "", 0xFF},
	{103, 78, "", 0xFF},
	{183, 52, "", 0xFF},
	{237, 18, "", 0xFF},
	{257, 62, "", 0xFF},
};

const uint16_t TRACE_COLORS[TRACE_COLOR_COUNT] = {
	0xFFDF,  // #F8FAFC white
	0xFE62,  // #FACC15 yellow
	0x269D,  // #22D3EE cyan
	0x4EF0,  // #4ADE80 green
	0xFC87,  // #FB923C orange
	0xF396,  // #F472B6 pink
};

uint16_t traceColor(uint8_t i) {
	return TRACE_COLORS[i < TRACE_COLOR_COUNT ? i : 0];
}

enum ChipStyle : uint8_t { ST_NORMAL, ST_FOCUS, ST_OK, ST_WARN, ST_ERR, ST_INFO };

uint16_t gridColor(int16_t rx, int16_t ry) {
	if (rx == 0 || rx == GRID_W - 1 || ry == 0 || ry == GRID_H - 1) return C_BORDER;
	bool vdiv = rx % DIV_PX == 0, hdiv = ry % DIV_PX == 0;
	int16_t dx = rx - MID_X, dy = ry - MID_Y;
	if ((dx >= -2 && dx <= 2 && ry % 5 == 0) || (dy >= -2 && dy <= 2 && rx % 5 == 0)) {
		return (dx == 0 || dy == 0) ? C_MUTED : C_BORDER;
	}
	if ((vdiv && ry % 5 == 0) || (hdiv && rx % 5 == 0)) return C_GRID_DOT;
	return C_BG;
}

/** Repaints grid rows [y0, y1] of column rx. */
void restoreColumn(int16_t rx, int16_t y0, int16_t y1) {
	if (y0 > y1) return;
	Tft::window(GRID_X + rx, GRID_Y + y0, GRID_X + rx, GRID_Y + y1);
	for (int16_t y = y0; y <= y1; y++) Tft::push(gridColor(rx, y));
	Tft::end();
}

void restoreRect(int16_t rx, int16_t ry, int16_t w, int16_t h) {
	if (rx < 0) { w += rx; rx = 0; }
	if (ry < 0) { h += ry; ry = 0; }
	if (rx + w > GRID_W) w = GRID_W - rx;
	if (ry + h > GRID_H) h = GRID_H - ry;
	if (w <= 0 || h <= 0) return;
	Tft::window(GRID_X + rx, GRID_Y + ry, GRID_X + rx + w - 1, GRID_Y + ry + h - 1);
	for (int16_t y = ry; y < ry + h; y++) {
		for (int16_t x = rx; x < rx + w; x++) Tft::push(gridColor(x, y));
	}
	Tft::end();
	// Restored pixels no longer hold trace, keep the span cache honest.
	for (int16_t x = rx; x < rx + w; x++) {
		if (oldTop[x] < 0) continue;
		if (oldTop[x] >= ry && oldBot[x] < ry + h) oldTop[x] = oldBot[x] = -1;
	}
}

void drawGrid() {
	Tft::window(GRID_X, GRID_Y, GRID_X + GRID_W - 1, GRID_Y + GRID_H - 1);
	for (int16_t y = 0; y < GRID_H; y++) {
		for (int16_t x = 0; x < GRID_W; x++) Tft::push(gridColor(x, y));
	}
	Tft::end();
}

void chip(ChipId id, const char *text, uint8_t style) {
	Chip &c = chips[id];
	if (c.style == style && strcmp(c.text, text) == 0) return;
	strncpy(c.text, text, sizeof c.text - 1);
	c.text[sizeof c.text - 1] = 0;
	c.style = style;
	uint16_t fill, fg;
	switch (style) {
	case ST_FOCUS: fill = C_ACCENT; fg = C_WHITE; break;
	case ST_OK: fill = C_OK_BG; fg = C_OK_FG; break;
	case ST_WARN: fill = C_WARN_BG; fg = C_WARN_FG; break;
	case ST_ERR: fill = C_ERR_BG; fg = C_ERR_FG; break;
	case ST_INFO: fill = C_TRACK; fg = C_ZERO; break;
	default: fill = C_TRACK; fg = C_TEXT; break;
	}
	Gfx::roundRect(c.x, STATUS_Y, c.w, FONT_M.height + 1, fill, C_BG);
	Gfx::textBox(FONT_M, text, c.x + 3, STATUS_Y, c.w - 6, ALIGN_CENTER, fg, fill);
}

int16_t valueToRow(const View &v, int32_t counts) {
	return MID_Y - v.s->vpos - int16_t((counts * DIV_PX) / COUNTS_PER_DIV);
}

void measureCell(int16_t x, int16_t w, const char *label, const char *value) {
	int16_t lx = Gfx::text(FONT_S, label, x, BAR_Y + 5, C_MUTED, C_BG);
	Tft::fillRect(lx, BAR_Y + 5, 4, FONT_S.height, C_BG);
	Gfx::textBox(FONT_M, value, lx + 4, BAR_Y + 2, x + w - lx - 4, ALIGN_LEFT, C_TEXT, C_BG);
}

void panel(int16_t x, int16_t y, int16_t w, int16_t h) {
	Gfx::roundRect(x, y, w, h, C_BORDER, C_BG);
	Gfx::roundRect(x + 1, y + 1, w - 2, h - 2, C_SURFACE, C_BORDER);
}

}  // namespace

namespace Ui {

int16_t triggerColumn(const Settings &s) {
	return MID_X + s.hpos;
}

void splash(uint8_t color) {
	Tft::fillScreen(C_BG);
	const int16_t y = 84;
	Gfx::textBox(FONT_L, "DSO138", 0, y, LCD_W, ALIGN_CENTER, C_WHITE, C_BG);
	Gfx::textBox(FONT_M, "Open Firmware " FW_VERSION, 0, y + 30, LCD_W, ALIGN_CENTER,
	             C_SUBTLE, C_BG);
	Gfx::textBox(FONT_S, I18n::tr(S_LICENSE), 0, y + 52, LCD_W, ALIGN_CENTER, C_MUTED, C_BG);
	// small sine accent
	static const int8_t WAVE[] = {0, 3, 5, 6, 5, 3, 0, -3, -5, -6, -5, -3};
	for (int16_t x = 0; x < 96; x++) {
		int16_t yy = y - 18 + WAVE[x % 12];
		Tft::fillRect(LCD_W / 2 - 48 + x, yy, 1, 2, traceColor(color));
	}
}

void layout() {
	Tft::fillScreen(C_BG);
	drawGrid();
	for (auto &c : chips) c.style = 0xFF;
	for (int16_t i = 0; i < GRID_W; i++) oldTop[i] = oldBot[i] = -1;
	oldZeroY = oldLevelY = -1;
	oldTrigX = -1000;
	menuH = 0;
	zeroDrawn = false;
}

void status(const View &v) {
	const Settings &s = *v.s;
	char buf[24];

	static const Str RUN_TEXT[] = {S_RUN, S_STOP, S_WAIT, S_ROLL};
	static const uint8_t RUN_STYLE[] = {ST_OK, ST_ERR, ST_WARN, ST_INFO};
	chip(CH_RUN, I18n::tr(RUN_TEXT[v.run]), RUN_STYLE[v.run]);

	Measure::formatDiv(buf, sizeof buf, TIMEBASE_S[s.timebase], "s");
	chip(CH_TB, buf, v.focus == FOCUS_TIMEBASE ? ST_FOCUS : ST_NORMAL);

	Measure::formatDiv(buf, sizeof buf, RANGE_VDIV[v.range], "V");
	static const Str CPL_TEXT[] = {S_GND, S_AC, S_DC};
	strncat(buf, "  ", sizeof buf - strlen(buf) - 1);
	strncat(buf, I18n::tr(CPL_TEXT[v.coupling]), sizeof buf - strlen(buf) - 1);
	chip(CH_VDIV, buf, ST_NORMAL);

	static const Str MODE_TEXT[] = {S_AUTO, S_NORMAL, S_SINGLE};
	chip(CH_MODE, I18n::tr(MODE_TEXT[s.trigMode]), v.focus == FOCUS_MODE ? ST_FOCUS : ST_NORMAL);
	chip(CH_EDGE, s.trigEdge == EDGE_RISING ? "\xE2\x86\x91" : "\xE2\x86\x93",
	     v.focus == FOCUS_EDGE ? ST_FOCUS : ST_NORMAL);

	Measure::format(buf, sizeof buf, s.trigLevel * voltsPerCount(v.range), "V");
	chip(CH_LEVEL, buf, v.focus == FOCUS_LEVEL ? ST_FOCUS : ST_NORMAL);
}

void trace(const View &v) {
	const Frame &f = *v.f;
	const Settings &s = *v.s;
	if (!f.n) return;
	const float pxTime = TIMEBASE_S[s.timebase] / DIV_PX;
	const int16_t trigCol = triggerColumn(s);
	const bool oneToOne = !f.roll && f.dt > pxTime * 0.999f && f.dt < pxTime * 1.001f;
	const float step = pxTime / f.dt;
	const bool gnd = f.coupling == CPL_GND;
	const uint16_t color = traceColor(s.traceColor);

	// Pass 1: row per column, -1 where there is no data
	static int16_t rows[GRID_W];
	for (int16_t x = 0; x < GRID_W; x++) {
		int32_t counts = 0;
		bool valid = true;
		if (gnd) {
			counts = 0;
		} else if (f.roll) {
			int32_t idx = int32_t(f.n) - GRID_W + x;
			valid = idx >= 0;
			if (valid) counts = int32_t(f.s[idx]) - f.zero;
		} else if (oneToOne) {
			int32_t idx = int32_t(f.trig) + x - trigCol;
			valid = idx >= 0 && idx < f.n;
			if (valid) counts = int32_t(f.s[idx]) - f.zero;
		} else {
			float p = f.trig + (x - trigCol) * step;
			int32_t i = int32_t(p);
			valid = p >= 0 && i + 1 < f.n;
			if (valid) {
				float frac = p - i;
				float c = f.s[i] + (float(f.s[i + 1]) - f.s[i]) * frac;
				counts = int32_t(c + 0.5f) - f.zero;
			}
		}
		if (!valid) {
			rows[x] = -1;
			continue;
		}
		int16_t y = valueToRow(v, counts);
		if (y < 1) y = 1;
		if (y > GRID_H - 2) y = GRID_H - 2;
		rows[x] = y;
	}

	// Pass 2: each column covers its row plus half the way to both
	// neighbours, so steep edges become continuous vertical strokes.
	for (int16_t x = 0; x < GRID_W; x++) {
		int16_t top = -1, bot = -1;
		int16_t y = rows[x];
		if (y >= 0) {
			top = bot = y;
			for (int8_t d = -1; d <= 1; d += 2) {
				int16_t nx = x + d;
				if (nx < 0 || nx >= GRID_W || rows[nx] < 0) continue;
				int16_t n = rows[nx];
				int16_t mid = y + (n - y) / 2;
				if (mid < top) top = mid;
				if (mid > bot) bot = mid;
			}
		}

		int16_t ot = oldTop[x], ob = oldBot[x];
		if (ot >= 0) {
			if (top < 0) {
				restoreColumn(x, ot, ob);
			} else {
				if (ot < top) restoreColumn(x, ot, (ob < top ? ob : top - 1));
				if (ob > bot) restoreColumn(x, (ot > bot ? ot : bot + 1), ob);
			}
		}
		if (top >= 0) {
			Tft::fillRect(GRID_X + x, GRID_Y + top, 1, bot - top + 1, color);
		}
		oldTop[x] = top;
		oldBot[x] = bot;
	}
}

void markers(const View &v) {
	const Settings &s = *v.s;
	constexpr uint8_t SIZE = 4;
	static int8_t oldFocus = -1;
	if (oldFocus != v.focus) {
		oldFocus = v.focus;
		oldZeroY = oldLevelY = -1;  // colors depend on focus
	}

	// Zero (ground) marker in the left margin
	int16_t zy = valueToRow(v, 0);
	if (zy < 0) zy = 0;
	if (zy > GRID_H - 1) zy = GRID_H - 1;
	if (zy != oldZeroY) {
		Tft::fillRect(0, GRID_Y - SIZE, GRID_X - 1, GRID_H + 2 * SIZE, C_BG);
		Gfx::marker(GRID_X - 2, GRID_Y + zy, SIZE, Gfx::DIR_RIGHT,
		            v.focus == FOCUS_VPOS ? C_WHITE : C_ZERO, C_BG);
		oldZeroY = zy;
	}

	// Trigger level marker in the right margin
	int16_t ly = valueToRow(v, s.trigLevel);
	if (ly < 0) ly = 0;
	if (ly > GRID_H - 1) ly = GRID_H - 1;
	if (ly != oldLevelY) {
		Tft::fillRect(GRID_X + GRID_W + 1, GRID_Y - SIZE, LCD_W - GRID_X - GRID_W - 1,
		              GRID_H + 2 * SIZE, C_BG);
		Gfx::marker(GRID_X + GRID_W + 1, GRID_Y + ly, SIZE, Gfx::DIR_LEFT,
		            v.focus == FOCUS_LEVEL ? C_WHITE : C_TRIG, C_BG);
		oldLevelY = ly;
	}

	// Trigger position marker hanging from the top border
	int16_t tx = triggerColumn(s);
	if (!v.f->roll && tx >= 0 && tx < GRID_W) {
		if (tx != oldTrigX && oldTrigX > -1000) restoreRect(oldTrigX - SIZE, 1, 2 * SIZE + 1, SIZE + 1);
		uint16_t c = v.focus == FOCUS_HPOS ? C_WHITE : C_TRIG;
		for (int16_t i = 0; i <= SIZE; i++) {
			int16_t half = SIZE - i;
			Tft::fillRect(GRID_X + tx - half, GRID_Y + 1 + i, 2 * half + 1, 1, c);
		}
		oldTrigX = tx;
	} else if (oldTrigX > -1000) {
		restoreRect(oldTrigX - SIZE, 1, 2 * SIZE + 1, SIZE + 1);
		oldTrigX = -1000;
	}
}

void measurements(const View &v) {
	if (!v.s->showMeasurements) {
		Tft::fillRect(0, BAR_Y, LCD_W, BAR_H, C_BG);
		return;
	}
	const Measurements &m = *v.m;

	// Cell widths follow the label lengths of the current language: each cell
	// reserves its label plus the widest value it can show, the rest of the
	// bar is shared out as spacing.
	static const Str LABELS[4] = {S_FREQ, S_VPP, S_VRMS, S_DUTY};
	static const char *const WIDEST[4] = {"888 kHz", "88.8 mV", "88.8 mV", "100 %"};
	int16_t w[4], sum = 0;
	for (uint8_t i = 0; i < 4; i++) {
		w[i] = Gfx::textWidth(FONT_S, I18n::tr(LABELS[i])) + 4 + Gfx::textWidth(FONT_M, WIDEST[i]);
		sum += w[i];
	}
	int16_t gap = (LCD_W - 8 - sum) / 3;
	if (gap < 2) gap = 2;

	char val[4][20];
	if (m.freq > 0) Measure::format(val[0], sizeof val[0], m.freq, "Hz");
	else strcpy(val[0], "--");
	Measure::format(val[1], sizeof val[1], m.vpp, "V");
	Measure::format(val[2], sizeof val[2], m.vrms, "V");
	if (m.freq > 0) {
		uint16_t d = uint16_t(m.duty * 100.0f + 0.5f);
		char *p = val[3];
		if (d >= 100) *p++ = char('0' + d / 100);
		if (d >= 10) *p++ = char('0' + (d / 10) % 10);
		*p++ = char('0' + d % 10);
		strcpy(p, " %");
	} else {
		strcpy(val[3], "--");
	}

	int16_t x = 4;
	for (uint8_t i = 0; i < 4; i++) {
		int16_t cw = i < 3 ? w[i] + gap : LCD_W - x;
		if (i == 3 && m.clipped) {
			Gfx::textBox(FONT_S, I18n::tr(S_CLIPPED), x, BAR_Y + 5, cw, ALIGN_LEFT, C_ERR_FG, C_BG);
			Tft::fillRect(x, BAR_Y, cw, 5, C_BG);
			Tft::fillRect(x, BAR_Y + 5 + FONT_S.height, cw, BAR_H - 5 - FONT_S.height, C_BG);
		} else {
			measureCell(x, cw, I18n::tr(LABELS[i]), val[i]);
		}
		x += cw;
	}
}

void hint(const char *text) {
	Gfx::textBox(FONT_M, text, 0, BAR_Y + 2, LCD_W, ALIGN_CENTER, C_SUBTLE, C_BG);
	Tft::fillRect(0, BAR_Y, LCD_W, 2, C_BG);
	Tft::fillRect(0, BAR_Y + 2 + FONT_M.height, LCD_W, BAR_H - 2 - FONT_M.height, C_BG);
}

void hint2(const char *label, const char *value) {
	char buf[64];
	strncpy(buf, label, sizeof buf - 1);
	buf[sizeof buf - 1] = 0;
	strncat(buf, ":  ", sizeof buf - strlen(buf) - 1);
	strncat(buf, value, sizeof buf - strlen(buf) - 1);
	hint(buf);
}

void menu(const char *title, const MenuItem *items, uint8_t count, uint8_t selected) {
	constexpr int16_t X = 40, W = 240, ROW = 18;
	const int16_t h = 30 + count * ROW + 8;
	const int16_t y0 = (LCD_H - h) / 2;
	if (menuH != h) {
		panel(X, y0, W, h);
		menuH = h;
	}
	Gfx::textBox(FONT_M, title, X + 12, y0 + 8, W - 24, ALIGN_LEFT, C_WHITE, C_SURFACE);
	Tft::fillRect(X + 12, y0 + 26, W - 24, 1, C_BORDER);
	for (uint8_t i = 0; i < count; i++) {
		int16_t y = y0 + 32 + i * ROW;
		bool sel = i == selected;
		uint16_t fill = sel ? C_ACCENT : C_SURFACE;
		Gfx::roundRect(X + 6, y, W - 12, ROW - 2, fill, C_SURFACE);
		int16_t vw = items[i].value ? Gfx::textWidth(FONT_M, items[i].value) + 6 : 0;
		Gfx::textBox(FONT_M, items[i].label, X + 12, y + 1, W - 24 - vw, ALIGN_LEFT,
		             sel ? C_WHITE : C_TEXT, fill);
		if (items[i].value) {
			Gfx::textBox(FONT_M, items[i].value, X + W - 12 - vw, y + 1, vw, ALIGN_RIGHT,
			             sel ? C_WHITE : C_ACCENT_LIGHT, fill);
		}
	}
}

void zeroCal(uint16_t done, Range current, Coupling c) {
	constexpr int16_t X = 30, Y = 34, W = 260, H = 172;
	static uint16_t lastDone = 0xFFFF;
	static int8_t lastCur = -1, lastCpl = -1;
	if (!zeroDrawn) {
		panel(X, Y, W, H);
		Gfx::textBox(FONT_M, I18n::tr(S_ZERO_CAL), X + 12, Y + 8, W - 24, ALIGN_LEFT, C_WHITE,
		             C_SURFACE);
		Gfx::textBox(FONT_S, I18n::tr(S_ZERO_HINT1), X + 12, Y + 30, W - 24, ALIGN_LEFT, C_SUBTLE,
		             C_SURFACE);
		Gfx::textBox(FONT_S, I18n::tr(S_ZERO_HINT2), X + 12, Y + 43, W - 24, ALIGN_LEFT, C_SUBTLE,
		             C_SURFACE);
		zeroDrawn = true;
		lastDone = 0xFFFF;
		lastCur = lastCpl = -1;
	}
	if (done == lastDone && current == lastCur && c == lastCpl) return;
	lastDone = done;
	lastCur = current;
	lastCpl = c;
	char buf[16];
	for (uint8_t r = 0; r < RANGE_COUNT; r++) {
		int16_t col = r % 3, row = r / 3;
		int16_t x = X + 12 + col * 80, y = Y + 64 + row * 24;
		bool ok = done & (1u << r);
		uint16_t fill = ok ? C_OK_BG : (r == current ? C_ACCENT : C_TRACK);
		uint16_t fg = ok ? C_OK_FG : (r == current ? C_WHITE : C_SUBTLE);
		Measure::formatDiv(buf, sizeof buf, RANGE_VDIV[r], "V");
		Gfx::roundRect(x, y, 74, FONT_M.height + 3, fill, C_SURFACE);
		Gfx::textBox(FONT_M, buf, x + 4, y + 2, 66, ALIGN_CENTER, fg, fill);
	}
	const char *msg;
	uint16_t fg = C_SUBTLE;
	if (done == (1u << RANGE_COUNT) - 1) {
		msg = I18n::tr(S_ZERO_DONE);
		fg = C_OK_FG;
	} else {
		static const Str CPL_TEXT[] = {S_GND, S_AC, S_DC};
		msg = I18n::tr(CPL_TEXT[c]);
		fg = c == CPL_GND ? C_OK_FG : C_WARN_FG;
	}
	Gfx::textBox(FONT_M, msg, X + 12, Y + H - 24, W - 24, ALIGN_CENTER, fg, C_SURFACE);
}

void genBar(const char *mode, const char *p0, const char *p1, uint8_t focus) {
	Tft::fillRect(0, BAR_Y, LCD_W, BAR_H, C_BG);
	int16_t x = Gfx::text(FONT_S, "GEN", 4, BAR_Y + 5, C_MUTED, C_BG) + 6;
	const char *items[3] = {mode, p0, p1};
	for (uint8_t i = 0; i < 3; i++) {
		if (!items[i] || !items[i][0]) continue;
		bool f = focus == i;
		uint16_t fill = f ? C_ACCENT : C_TRACK;
		int16_t w = Gfx::textWidth(FONT_M, items[i]) + 14;
		if (x + w > LCD_W - 2) w = LCD_W - 2 - x;
		Gfx::roundRect(x, BAR_Y + 1, w, FONT_M.height + 1, fill, C_BG);
		Gfx::textBox(FONT_M, items[i], x + 4, BAR_Y + 1, w - 8, ALIGN_CENTER, f ? C_WHITE : C_TEXT, fill);
		x += w + 4;
	}
}

void about() {
	constexpr int16_t X = 40, Y = 50, W = 240, H = 140;
	panel(X, Y, W, H);
	Gfx::textBox(FONT_L, "DSO138", X + 12, Y + 12, W - 24, ALIGN_CENTER, C_WHITE, C_SURFACE);
	Gfx::textBox(FONT_M, "Open Firmware " FW_VERSION, X + 12, Y + 44, W - 24, ALIGN_CENTER,
	             C_SUBTLE, C_SURFACE);
	Gfx::textBox(FONT_S, I18n::tr(S_LICENSE), X + 12, Y + 68, W - 24, ALIGN_CENTER, C_MUTED,
	             C_SURFACE);
	Gfx::textBox(FONT_S, "github.com/sickdaflip/dso0138", X + 12, Y + 84, W - 24, ALIGN_CENTER,
	             C_ACCENT_LIGHT, C_SURFACE);
	Gfx::textBox(FONT_S, I18n::tr(S_HINT_SEL), X + 12, Y + 106, W - 24, ALIGN_CENTER, C_MUTED,
	             C_SURFACE);
	Gfx::textBox(FONT_S, I18n::tr(S_HINT_OK), X + 12, Y + 120, W - 24, ALIGN_CENTER, C_MUTED,
	             C_SURFACE);
}

}  // namespace Ui
