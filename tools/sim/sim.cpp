// SPDX-License-Identifier: MIT
/**
 * Host simulator: renders UI screens into PPM images for previews.
 * Build and run through tools/sim/render.sh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/i18n.h"
#include "../../src/measure.h"
#include "../../src/tft.h"
#include "../../src/ui.h"

static uint16_t fb[LCD_H][LCD_W];
static int16_t wx0, wy0, wx1, wy1, cx, cy;

namespace Tft {
void begin() {}
void window(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	wx0 = x0; wy0 = y0; wx1 = x1; wy1 = y1; cx = x0; cy = y0;
}
void end() {}
void push(uint16_t c) {
	if (cx >= 0 && cx < LCD_W && cy >= 0 && cy < LCD_H) fb[cy][cx] = c;
	if (++cx > wx1) { cx = wx0; cy++; }
}
void pushN(uint16_t c, uint32_t n) { while (n--) push(c); }
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > LCD_W) w = LCD_W - x;
	if (y + h > LCD_H) h = LCD_H - y;
	if (w <= 0 || h <= 0) return;
	window(x, y, x + w - 1, y + h - 1);
	pushN(c, uint32_t(w) * h);
}
void fillScreen(uint16_t c) { fillRect(0, 0, LCD_W, LCD_H, c); }
}  // namespace Tft

static void save(const char *name) {
	FILE *f = fopen(name, "wb");
	fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
	for (int y = 0; y < LCD_H; y++)
		for (int x = 0; x < LCD_W; x++) {
			uint16_t c = fb[y][x];
			uint8_t rgb[3] = {uint8_t((c >> 11) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63),
			                  uint8_t((c & 31) * 255 / 31)};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

static Frame frame;
static Settings cfg;
static Measurements meas;

enum Wave { SINE, SQUARE, TRI };

static void synth(Wave w, float freq, float amp, float offset, Range r, float noise) {
	frame.n = FRAME_MAX;
	frame.trig = FRAME_PRE;
	frame.dt = TIMEBASE_S[cfg.timebase] / DIV_PX;
	frame.range = r;
	frame.coupling = CPL_DC;
	frame.zero = cfg.zero[r];
	frame.triggered = true;
	frame.roll = false;
	float vpc = voltsPerCount(r);
	srand(7);
	for (int i = 0; i < FRAME_MAX; i++) {
		float t = (i - FRAME_PRE) * frame.dt;
		float ph = fmodf(t * freq + 10.0f, 1.0f);
		float v;
		switch (w) {
		case SINE: v = sinf(ph * 6.2831853f); break;
		case SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; v *= 1.0f - 0.25f * expf(-(fmodf(ph, 0.5f)) * 60); break;
		default: v = ph < 0.5f ? ph * 4 - 1 : 3 - ph * 4; break;
		}
		v = v * amp + offset + noise * ((rand() % 1000) / 1000.0f - 0.5f);
		int c = int(v / vpc + frame.zero);
		frame.s[i] = uint16_t(c < 0 ? 0 : c > 4095 ? 4095 : c);
	}
}

static View view(Range r, RunState run, Focus focus) {
	View v;
	v.s = &cfg; v.f = &frame; v.m = &meas; v.range = r; v.coupling = CPL_DC;
	v.run = run; v.focus = focus;
	return v;
}

static void scope(const char *file, Range r, RunState run, Focus focus) {
	Measure::compute(frame, meas);
	View v = view(r, run, focus);
	Ui::layout();
	Ui::status(v);
	Ui::trace(v);
	Ui::markers(v);
	Ui::measurements(v);
	save(file);
}

int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[256];
	settingsDefaults(cfg);
	uint8_t lang = argc > 2 ? uint8_t(atoi(argv[2])) : 0;
	I18n::set(lang);
	cfg.language = lang;

	Ui::splash();
	snprintf(path, sizeof path, "%s/01_boot.ppm", dir); save(path);

	// 1 kHz test signal (3.3 V square) at 1 V/div, 0.2 ms/div
	cfg.timebase = 4;
	cfg.trigLevel = 160;
	cfg.vpos = -40;
	synth(SQUARE, 1000, 1.65f, 1.65f, RNG_1V, 0.04f);
	snprintf(path, sizeof path, "%s/02_square.ppm", dir);
	scope(path, RNG_1V, RS_RUN, FOCUS_TIMEBASE);

	// 50 Hz sine at 0.5 V/div, 5 ms/div, trigger level focused
	cfg.timebase = 8;
	cfg.trigLevel = 40;
	cfg.vpos = 0;
	synth(SINE, 50, 1.4f, 0, RNG_500mV, 0.03f);
	snprintf(path, sizeof path, "%s/03_sine.ppm", dir);
	scope(path, RNG_500mV, RS_RUN, FOCUS_LEVEL);

	// Triangle, stopped, normal mode
	cfg.timebase = 6;
	cfg.trigMode = TRIG_NORMAL;
	cfg.trigEdge = EDGE_FALLING;
	cfg.trigLevel = -20;
	cfg.hpos = -80;
	synth(TRI, 440, 0.15f, 0.02f, RNG_50mV, 0.006f);
	snprintf(path, sizeof path, "%s/04_triangle_stop.ppm", dir);
	scope(path, RNG_50mV, RS_STOP, FOCUS_HPOS);

	// Menu
	settingsDefaults(cfg);
	cfg.language = lang;
	cfg.timebase = 4;
	synth(SQUARE, 1000, 1.65f, 1.65f, RNG_1V, 0.04f);
	snprintf(path, sizeof path, "%s/menu_bg.tmp", dir);
	scope(path, RNG_1V, RS_STOP, FOCUS_TIMEBASE);
	Ui::MenuItem items[] = {
		{I18n::tr(S_LANGUAGE), I18n::langName(lang)},
		{I18n::tr(S_MEASUREMENTS), I18n::tr(S_ON)},
		{I18n::tr(S_ZERO_CAL), nullptr},
		{I18n::tr(S_CENTER), nullptr},
		{I18n::tr(S_FACTORY), nullptr},
		{I18n::tr(S_ABOUT), nullptr},
		{I18n::tr(S_BACK), nullptr},
	};
	Ui::menu(I18n::tr(S_MENU), items, 7, 2);
	snprintf(path, sizeof path, "%s/05_menu.ppm", dir); save(path);

	Ui::layout();
	Ui::zeroCal(0x1F, RNG_100mV, CPL_GND);
	snprintf(path, sizeof path, "%s/06_zero_cal.ppm", dir); save(path);
	return 0;
}
