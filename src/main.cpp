// SPDX-License-Identifier: MIT
/**
 * DSO138 open firmware: application state machine.
 */
#include <string.h>
#include "acq.h"
#include "buttons.h"
#include "hal.h"
#include "i18n.h"
#include "measure.h"
#include "storage.h"
#include "tft.h"
#include "ui.h"

namespace {

enum Screen : uint8_t { SCR_SCOPE, SCR_MENU, SCR_ZERO, SCR_ABOUT };

enum MenuEntry : uint8_t {
	MI_LANGUAGE, MI_MEASURE, MI_COLOR, MI_ZERO, MI_CENTER, MI_FACTORY, MI_ABOUT, MI_BACK, MI_COUNT
};

constexpr uint32_t SWITCH_POLL_MS = 100;
constexpr uint32_t SAVE_DELAY_MS = 2000;
constexpr uint32_t HINT_MS = 1500;
constexpr uint32_t WAIT_SHOW_MS = 150;
constexpr uint32_t STREAM_MS = 500;
constexpr int16_t HPOS_LIMIT = 350;
constexpr int16_t VPOS_LIMIT = 120;
constexpr int16_t LEVEL_LIMIT = 400;

Settings cfg;
Measurements meas;
Screen screen = SCR_SCOPE;
Focus focus = FOCUS_TIMEBASE;
bool running = true;
bool haveFrame = false;
uint8_t menuSel = 0;

Range range = RNG_1V;
Coupling coupling = CPL_DC;
uint32_t lastSwitch = 0;

bool dirty = false;
uint32_t dirtySince = 0;
uint32_t hintUntil = 0;
uint32_t lastFrameAt = 0;
uint32_t captureStart = 0;

uint16_t zeroDone = 0;
int8_t zeroStableRange = -1;

bool streaming = false;
uint32_t lastStream = 0;
char cmd[16];
uint8_t cmdLen = 0;

View view() {
	View v;
	v.s = &cfg;
	v.f = &Acq::frame();
	v.m = &meas;
	v.range = range;
	v.coupling = coupling;
	v.focus = focus;
	if (!running) v.run = RS_STOP;
	else if (Acq::state() == Acq::ACQ_ROLL) v.run = RS_ROLL;
	else if (Acq::state() != Acq::ACQ_IDLE && hal::millis() - lastFrameAt > WAIT_SHOW_MS &&
	         hal::millis() - captureStart > WAIT_SHOW_MS && cfg.trigMode != TRIG_AUTO)
		v.run = RS_WAIT;
	else v.run = RS_RUN;
	return v;
}

void markDirty() {
	dirty = true;
	dirtySince = hal::millis();
}

void showHint(const char *text) {
	Ui::hint(text);
	hintUntil = hal::millis() + HINT_MS;
}

void showHint2(const char *label, const char *value) {
	Ui::hint2(label, value);
	hintUntil = hal::millis() + HINT_MS;
}

void startCapture() {
	captureStart = hal::millis();
	int16_t lvl = cfg.zero[range] + cfg.trigLevel;
	Acq::start(cfg, lvl, range, coupling, cfg.zero[range]);
}

void redrawFrame() {
	View v = view();
	if (haveFrame) Ui::trace(v);
	Ui::markers(v);
}

void drawScope() {
	Ui::layout();
	View v = view();
	Ui::status(v);
	redrawFrame();
	Ui::measurements(v);
}

// ---------------------------------------------------------------------------
// Serial protocol (115200 8N1, line based)
//   i  -> "DSO138 Open <version>"
//   w  -> one frame: "#frame ..." header, comma separated counts, "#end"
//   m  -> measurements
//   s1 / s0 -> stream frames on / off
//   boot -> "#boot", then jump to the STM32 serial bootloader
// ---------------------------------------------------------------------------
void putFloat(float v) {
	char buf[24];
	// fixed scientific-ish output without printf: mantissa * 10^exp
	bool neg = v < 0;
	if (neg) v = -v;
	int8_t exp = 0;
	if (v != 0.0f) {
		while (v >= 10.0f) { v /= 10.0f; exp++; }
		while (v < 1.0f) { v *= 10.0f; exp--; }
	}
	uint32_t mant = uint32_t(v * 1000000.0f + 0.5f);
	char *p = buf;
	if (neg) *p++ = '-';
	*p++ = char('0' + mant / 1000000);
	*p++ = '.';
	for (uint32_t d = 100000; d; d /= 10) *p++ = char('0' + (mant / d) % 10);
	*p++ = 'e';
	if (exp < 0) { *p++ = '-'; exp = -exp; }
	if (exp >= 10) *p++ = char('0' + exp / 10);
	*p++ = char('0' + exp % 10);
	*p = 0;
	hal::serialWrite(buf);
}

void putInt(int32_t v) {
	char buf[12];
	char *p = buf + sizeof buf - 1;
	*p = 0;
	bool neg = v < 0;
	uint32_t u = neg ? uint32_t(-v) : uint32_t(v);
	do {
		*--p = char('0' + u % 10);
		u /= 10;
	} while (u);
	if (neg) *--p = '-';
	hal::serialWrite(p);
}

void sendFrame() {
	const Frame &f = Acq::frame();
	hal::serialWrite("#frame dt=");
	putFloat(f.dt);
	hal::serialWrite(" n=");
	putInt(f.n);
	hal::serialWrite(" trig=");
	putInt(f.trig);
	hal::serialWrite(" zero=");
	putInt(f.zero);
	hal::serialWrite(" vpc=");
	putFloat(voltsPerCount(f.range));
	hal::serialWrite(" vdiv=");
	putFloat(RANGE_VDIV[f.range]);
	hal::serialWrite(" tdiv=");
	putFloat(TIMEBASE_S[cfg.timebase]);
	hal::serialWrite(" cpl=");
	putInt(f.coupling);
	hal::serialWrite(" roll=");
	putInt(f.roll);
	hal::serialWrite(" col=");
	putInt(Ui::triggerColumn(cfg));
	hal::serialWrite(" color=");
	putInt(cfg.traceColor);
	hal::serialWrite("\r\n");
	for (uint16_t i = 0; i < f.n; i++) {
		putInt(f.s[i]);
		hal::serialPutc((i % 32 == 31 || i == f.n - 1) ? '\n' : ',');
	}
	hal::serialWrite("#end\r\n");
}

void sendMeasurements() {
	hal::serialWrite("#meas freq=");
	putFloat(meas.freq);
	hal::serialWrite(" vpp=");
	putFloat(meas.vpp);
	hal::serialWrite(" vrms=");
	putFloat(meas.vrms);
	hal::serialWrite(" vavg=");
	putFloat(meas.vavg);
	hal::serialWrite(" vmax=");
	putFloat(meas.vmax);
	hal::serialWrite(" vmin=");
	putFloat(meas.vmin);
	hal::serialWrite(" duty=");
	putFloat(meas.duty);
	hal::serialWrite("\r\n");
}

void handleCommand(const char *c) {
	if (!strcmp(c, "i") || !strcmp(c, "?")) {
		hal::serialWrite(FW_NAME " " FW_VERSION "\r\n");
	} else if (!strcmp(c, "w")) {
		if (haveFrame) sendFrame();
		else hal::serialWrite("#none\r\n");
	} else if (!strcmp(c, "m")) {
		sendMeasurements();
	} else if (!strcmp(c, "boot")) {
		Acq::stop();
		Tft::fillScreen(0);
		Ui::hint("Bootloader");
		hal::serialWrite("#boot\r\n");
		hal::enterBootloader();
	} else if (!strcmp(c, "s1")) {
		streaming = true;
		hal::serialWrite("#ok\r\n");
	} else if (!strcmp(c, "s0")) {
		streaming = false;
		hal::serialWrite("#ok\r\n");
	} else if (c[0]) {
		hal::serialWrite("#error unknown command\r\n");
	}
}

void pollSerial() {
	int ch;
	while ((ch = hal::serialRead()) >= 0) {
		if (ch == '\r' || ch == '\n') {
			cmd[cmdLen] = 0;
			handleCommand(cmd);
			cmdLen = 0;
		} else if (cmdLen < sizeof cmd - 1) {
			cmd[cmdLen++] = char(ch);
		}
	}
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void drawMenu() {
	Ui::MenuItem items[MI_COUNT] = {
		{I18n::tr(S_LANGUAGE), I18n::langName(cfg.language)},
		{I18n::tr(S_MEASUREMENTS), I18n::tr(cfg.showMeasurements ? S_ON : S_OFF)},
		{I18n::tr(S_TRACE_COLOR), I18n::tr(Str(S_COL_WHITE + cfg.traceColor % TRACE_COLOR_COUNT))},
		{I18n::tr(S_ZERO_CAL), nullptr},
		{I18n::tr(S_CENTER), nullptr},
		{I18n::tr(S_FACTORY), nullptr},
		{I18n::tr(S_ABOUT), nullptr},
		{I18n::tr(S_BACK), nullptr},
	};
	Ui::menu(I18n::tr(S_MENU), items, MI_COUNT, menuSel);
}

void openScope() {
	screen = SCR_SCOPE;
	drawScope();
	if (running) startCapture();
}

void openMenu() {
	screen = SCR_MENU;
	menuSel = 0;
	Acq::stop();
	drawMenu();
}

void openZeroCal() {
	screen = SCR_ZERO;
	zeroDone = 0;
	zeroStableRange = -1;
	Ui::layout();
	Ui::zeroCal(zeroDone, range, coupling);
	Settings tmp = cfg;
	tmp.timebase = TB_DEFAULT;
	tmp.trigMode = TRIG_AUTO;
	Acq::start(tmp, 0, range, coupling, cfg.zero[range]);
}

void menuActivate() {
	switch (menuSel) {
	case MI_LANGUAGE:
		cfg.language = (cfg.language + 1) % LANG_COUNT;
		I18n::set(cfg.language);
		markDirty();
		Ui::layout();  // labels may change width
		drawMenu();
		break;
	case MI_MEASURE:
		cfg.showMeasurements = !cfg.showMeasurements;
		markDirty();
		drawMenu();
		break;
	case MI_COLOR:
		cfg.traceColor = (cfg.traceColor + 1) % TRACE_COLOR_COUNT;
		markDirty();
		drawMenu();
		break;
	case MI_ZERO:
		openZeroCal();
		break;
	case MI_CENTER:
		cfg.vpos = 0;
		cfg.hpos = 0;
		cfg.trigLevel = 0;
		markDirty();
		openScope();
		showHint(I18n::tr(S_CENTERED));
		break;
	case MI_FACTORY: {
		int16_t zero[RANGE_COUNT];
		memcpy(zero, cfg.zero, sizeof zero);
		settingsDefaults(cfg);
		memcpy(cfg.zero, zero, sizeof zero);  // keep the calibration
		I18n::set(cfg.language);
		markDirty();
		openScope();
		showHint(I18n::tr(S_RESET_DONE));
		break;
	}
	case MI_ABOUT:
		screen = SCR_ABOUT;
		Ui::layout();
		Ui::about();
		break;
	default:
		openScope();
		break;
	}
}

// ---------------------------------------------------------------------------
// Scope controls
// ---------------------------------------------------------------------------
template <typename T>
T clampT(T v, T lo, T hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

int16_t accel() {
	uint16_t r = Buttons::repeatCount();
	return r > 30 ? 10 : r > 10 ? 4 : 1;
}

void adjust(int8_t dir) {
	char buf[24];
	bool restart = false;
	switch (focus) {
	case FOCUS_TIMEBASE: {
		int16_t tb = clampT<int16_t>(cfg.timebase + dir, 0, TIMEBASE_COUNT - 1);
		if (tb == cfg.timebase) return;
		cfg.timebase = uint8_t(tb);
		restart = true;
		break;
	}
	case FOCUS_MODE:
		cfg.trigMode = uint8_t((cfg.trigMode + TRIG_MODE_COUNT + dir) % TRIG_MODE_COUNT);
		restart = true;
		break;
	case FOCUS_EDGE:
		cfg.trigEdge = cfg.trigEdge == EDGE_RISING ? EDGE_FALLING : EDGE_RISING;
		restart = true;
		break;
	case FOCUS_LEVEL:
		cfg.trigLevel = clampT<int16_t>(cfg.trigLevel + dir * 4 * accel(), -LEVEL_LIMIT, LEVEL_LIMIT);
		restart = true;
		break;
	case FOCUS_VPOS:
		cfg.vpos = clampT<int16_t>(cfg.vpos + dir * accel(), -VPOS_LIMIT, VPOS_LIMIT);
		Measure::format(buf, sizeof buf, cfg.vpos * RANGE_VDIV[range] / DIV_PX, "V");
		showHint2(I18n::tr(S_FOCUS_VPOS), buf);
		break;
	case FOCUS_HPOS:
		cfg.hpos = clampT<int16_t>(cfg.hpos + dir * accel(), -HPOS_LIMIT, HPOS_LIMIT);
		Measure::format(buf, sizeof buf, cfg.hpos * TIMEBASE_S[cfg.timebase] / DIV_PX, "s");
		showHint2(I18n::tr(S_FOCUS_HPOS), buf);
		break;
	default:
		break;
	}
	markDirty();
	View v = view();
	Ui::status(v);
	redrawFrame();
	if (restart && running) startCapture();
}

void resetFocused() {
	switch (focus) {
	case FOCUS_LEVEL: cfg.trigLevel = 0; break;
	case FOCUS_VPOS: cfg.vpos = 0; break;
	case FOCUS_HPOS: cfg.hpos = 0; break;
	case FOCUS_TIMEBASE: cfg.timebase = TB_DEFAULT; break;
	case FOCUS_MODE: cfg.trigMode = TRIG_AUTO; break;
	case FOCUS_EDGE: cfg.trigEdge = EDGE_RISING; break;
	default: break;
	}
	markDirty();
	View v = view();
	Ui::status(v);
	redrawFrame();
	if (running) startCapture();
}

void handleKey(Key k) {
	switch (screen) {
	case SCR_SCOPE:
		switch (k) {
		case KEY_SEL: {
			focus = Focus((focus + 1) % FOCUS_COUNT);
			static const Str NAMES[FOCUS_COUNT] = {S_FOCUS_TB, S_FOCUS_MODE, S_FOCUS_EDGE,
			                                       S_FOCUS_LEVEL, S_FOCUS_VPOS, S_FOCUS_HPOS};
			View v = view();
			Ui::status(v);
			Ui::markers(v);
			showHint(I18n::tr(NAMES[focus]));
			break;
		}
		case KEY_SEL_LONG: resetFocused(); break;
		case KEY_PLUS: adjust(+1); break;
		case KEY_MINUS: adjust(-1); break;
		case KEY_OK:
			running = !running;
			if (running) startCapture();
			else Acq::stop();
			Ui::status(view());
			break;
		case KEY_OK_LONG: openMenu(); break;
		default: break;
		}
		break;

	case SCR_MENU:
		switch (k) {
		case KEY_PLUS: menuSel = (menuSel + 1) % MI_COUNT; drawMenu(); break;
		case KEY_MINUS: menuSel = (menuSel + MI_COUNT - 1) % MI_COUNT; drawMenu(); break;
		case KEY_OK: menuActivate(); break;
		case KEY_SEL:
		case KEY_OK_LONG: openScope(); break;
		default: break;
		}
		break;

	case SCR_ZERO:
	case SCR_ABOUT:
		if (k == KEY_OK || k == KEY_SEL || k == KEY_OK_LONG) {
			if (screen == SCR_ZERO && zeroDone) markDirty();
			Acq::stop();
			openScope();
		}
		break;
	}
}

void pollSwitches(uint32_t now) {
	if (now - lastSwitch < SWITCH_POLL_MS) return;
	lastSwitch = now;
	Acq::Switches sw = Acq::readSwitches();
	if (sw.range == range && sw.coupling == coupling) return;
	range = sw.range;
	coupling = sw.coupling;
	if (screen == SCR_SCOPE) {
		Ui::status(view());
		redrawFrame();
		if (running) startCapture();
	} else if (screen == SCR_ZERO) {
		zeroStableRange = -1;
		Ui::zeroCal(zeroDone, range, coupling);
		Settings tmp = cfg;
		tmp.timebase = TB_DEFAULT;
		tmp.trigMode = TRIG_AUTO;
		Acq::start(tmp, 0, range, coupling, cfg.zero[range]);
	}
}

void onFrame(uint32_t now) {
	lastFrameAt = now;
	haveFrame = true;
	const Frame &f = Acq::frame();
	Measure::compute(f, meas);
	View v = view();
	Ui::trace(v);
	Ui::markers(v);
	if (int32_t(now - hintUntil) >= 0) Ui::measurements(v);
	if (streaming && now - lastStream >= STREAM_MS) {
		lastStream = now;
		sendFrame();
	}
	if (f.roll) return;
	if (dirty && now - dirtySince >= SAVE_DELAY_MS) {
		dirty = false;
		Storage::save(cfg);  // acquisition is idle right now
	}
	if (cfg.trigMode == TRIG_SINGLE && f.triggered) {
		running = false;
	} else {
		startCapture();
	}
	Ui::status(view());
}

void onZeroFrame() {
	if (coupling != CPL_GND) return;
	// Use the second frame after a switch change, the first may still settle.
	if (zeroStableRange != range) {
		zeroStableRange = range;
	} else {
		cfg.zero[range] = Acq::frameMean();
		zeroDone |= 1u << range;
		Ui::zeroCal(zeroDone, range, coupling);
	}
	Settings tmp = cfg;
	tmp.timebase = TB_DEFAULT;
	tmp.trigMode = TRIG_AUTO;
	Acq::start(tmp, 0, range, coupling, cfg.zero[range]);
}

}  // namespace

int main() {
	hal::bootCheck();  // jumps to the ROM bootloader after a "boot" request
	hal::init();
	hal::startPwm();  // negative rail for the analog front end first
	Tft::begin();
	Buttons::begin();
	hal::serialInit(SERIAL_BAUD);
	Storage::load(cfg);
	I18n::set(cfg.language);
	Acq::begin();
	Ui::splash(cfg.traceColor);
	hal::delayMs(1200);

	Acq::Switches sw = Acq::readSwitches();
	range = sw.range;
	coupling = sw.coupling;
	drawScope();
	startCapture();
	hal::serialWrite(FW_NAME " " FW_VERSION "\r\n");

	uint32_t lastStatus = 0;
	for (;;) {
		uint32_t now = hal::millis();
		Key k = Buttons::poll(now);
		if (k != KEY_NONE) handleKey(k);
		pollSwitches(now);
		pollSerial();

		if (screen == SCR_SCOPE) {
			if (running && Acq::poll(now)) onFrame(now);
			if (now - lastStatus >= 100) {
				lastStatus = now;
				Ui::status(view());
			}
			if (hintUntil && int32_t(now - hintUntil) >= 0) {
				hintUntil = 0;
				Ui::measurements(view());
			}
		} else if (screen == SCR_ZERO) {
			if (Acq::poll(now)) onZeroFrame();
		}

		if (dirty && now - dirtySince >= SAVE_DELAY_MS && Acq::state() == Acq::ACQ_IDLE) {
			dirty = false;
			Storage::save(cfg);
		} else if (dirty && now - dirtySince >= 5 * SAVE_DELAY_MS) {
			// long captures (roll/slow) would block saving forever
			dirty = false;
			Acq::stop();
			Storage::save(cfg);
			if (screen == SCR_SCOPE && running) startCapture();
		}
		hal::led(Acq::state() == Acq::ACQ_POST || Acq::state() == Acq::ACQ_ROLL);
	}
}
