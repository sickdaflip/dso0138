// SPDX-License-Identifier: MIT
/**
 * Screen layout (320 x 240):
 *   y   0..17   status bar: run state, timebase, V/div + coupling, trigger
 *   y  19..218  12 x 8 division grid (25 px), markers left/right/top
 *   y 221..239  measurement bar or hint line
 */
#pragma once
#include "measure.h"
#include "model.h"

enum RunState : uint8_t { RS_RUN, RS_STOP, RS_WAIT, RS_ROLL };

enum Focus : uint8_t {
	FOCUS_TIMEBASE, FOCUS_MODE, FOCUS_EDGE, FOCUS_LEVEL, FOCUS_VPOS, FOCUS_HPOS, FOCUS_COUNT
};

struct View {
	const Settings *s;
	const Frame *f;
	const Measurements *m;
	Range range;
	Coupling coupling;
	RunState run;
	Focus focus;
};

namespace Ui {

void splash(uint8_t traceColor);

/** Clears the screen and draws the static layout; forgets the old trace. */
void layout();

void status(const View &v);
void trace(const View &v);
void markers(const View &v);
void measurements(const View &v);

/** Replaces the measurement bar with a hint until the next measurements(). */
void hint(const char *text);
void hint2(const char *label, const char *value);

// Settings menu ------------------------------------------------------------
struct MenuItem {
	const char *label;
	const char *value;  // may be null
};
void menu(const char *title, const MenuItem *items, uint8_t count, uint8_t selected);

/** Zero calibration panel: `done` bit mask of calibrated ranges. */
void zeroCal(uint16_t done, Range current, Coupling c);

void about();

/** Screen column of the trigger point. */
int16_t triggerColumn(const Settings &s);

}  // namespace Ui
