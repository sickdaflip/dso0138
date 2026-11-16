// SPDX-License-Identifier: MIT
#include "model.h"

const float RANGE_VDIV[RANGE_COUNT] = {5.0f, 2.0f, 1.0f, 0.5f, 0.2f, 0.1f, 0.05f, 0.02f, 0.01f};

const float TIMEBASE_S[TIMEBASE_COUNT] = {
	10e-6f, 20e-6f, 50e-6f, 100e-6f, 200e-6f, 500e-6f,
	1e-3f, 2e-3f, 5e-3f, 10e-3f, 20e-3f, 50e-3f,
	100e-3f, 200e-3f, 500e-3f, 1.0f, 2.0f, 5.0f, 10.0f,
};

// The front-end divider is about 1.7 % off the nominal 100 counts/div; these
// factors match the gain of the stock JYE board.
static const float GAIN_TRIM = 1.017f;

float voltsPerCount(Range r) {
	return RANGE_VDIV[r] * GAIN_TRIM / COUNTS_PER_DIV;
}

void settingsDefaults(Settings &s) {
	s.timebase = TB_DEFAULT;
	s.trigMode = TRIG_AUTO;
	s.trigEdge = EDGE_RISING;
	s.language = 0;
	s.trigLevel = 0;
	s.vpos = 0;
	s.hpos = 0;
	for (auto &z : s.zero) z = 2060;  // about 1.66 V at the ADC input
	s.showMeasurements = 1;
	s.traceColor = 0;
	for (auto &r : s.reserved) r = 0;
}
