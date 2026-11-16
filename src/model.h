// SPDX-License-Identifier: MIT
/**
 * Scope model: persistent settings, range and timebase tables and the
 * captured frame shared between acquisition, measurements and the UI.
 */
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Input ranges (selected by the SEN1/SEN2 slide switches)
// ---------------------------------------------------------------------------
enum Range : uint8_t {
	RNG_5V, RNG_2V, RNG_1V, RNG_500mV, RNG_200mV, RNG_100mV, RNG_50mV, RNG_20mV, RNG_10mV,
	RANGE_COUNT
};

enum Coupling : uint8_t { CPL_GND, CPL_AC, CPL_DC };

/** Nominal volts per division for each range. */
extern const float RANGE_VDIV[RANGE_COUNT];

/** The front end maps one division to about 100 ADC counts on every range. */
constexpr int16_t COUNTS_PER_DIV = 100;

// ---------------------------------------------------------------------------
// Timebases
// ---------------------------------------------------------------------------
constexpr uint8_t TIMEBASE_COUNT = 19;
extern const float TIMEBASE_S[TIMEBASE_COUNT];  // seconds per division
constexpr uint8_t TB_DEFAULT = 6;               // 1 ms/div
constexpr uint8_t TB_FAST_LAST = 1;             // 10 and 20 us/div: interleaved ADCs
constexpr uint8_t TB_ROLL_FIRST = 12;           // >= 100 ms/div: roll mode in Auto

// ---------------------------------------------------------------------------
// Grid geometry
// ---------------------------------------------------------------------------
constexpr int16_t DIV_PX = 25;
constexpr int16_t GRID_DIVS_X = 12;
constexpr int16_t GRID_DIVS_Y = 8;
constexpr int16_t GRID_W = DIV_PX * GRID_DIVS_X;  // 300
constexpr int16_t GRID_H = DIV_PX * GRID_DIVS_Y;  // 200
constexpr int16_t GRID_X = 10;
constexpr int16_t GRID_Y = 19;
constexpr int16_t SAMPLES_PER_DIV = DIV_PX;      // timer modes: one sample per pixel

// ---------------------------------------------------------------------------
// Trigger
// ---------------------------------------------------------------------------
enum TrigMode : uint8_t { TRIG_AUTO, TRIG_NORMAL, TRIG_SINGLE, TRIG_MODE_COUNT };
enum TrigEdge : uint8_t { EDGE_RISING, EDGE_FALLING };

// ---------------------------------------------------------------------------
// Persistent settings
// ---------------------------------------------------------------------------
struct Settings {
	uint8_t timebase;
	uint8_t trigMode;
	uint8_t trigEdge;
	uint8_t language;
	int16_t trigLevel;               // ADC counts relative to zero
	int16_t vpos;                    // trace offset in pixels (up = positive)
	int16_t hpos;                    // trigger column offset from the grid center (px)
	int16_t zero[RANGE_COUNT];       // ADC count at 0 V for each range
	uint8_t showMeasurements;
	uint8_t traceColor;              // index into the trace palette
	uint8_t holdoff;                 // index into HOLDOFF_S, 0 = off
	uint8_t reserved[1];
};

void settingsDefaults(Settings &s);

/**
 * Trigger holdoff: the signal must have stayed on the far side of the
 * trigger level for at least this long before the edge counts. This picks
 * the first edge of a burst or of a serial frame.
 */
constexpr uint8_t HOLDOFF_COUNT = 17;
extern const float HOLDOFF_S[HOLDOFF_COUNT];

/** Trace colors selectable in the settings menu (white is the default). */
constexpr uint8_t TRACE_COLOR_COUNT = 6;

// ---------------------------------------------------------------------------
// Captured frame
// ---------------------------------------------------------------------------
constexpr uint16_t FRAME_MAX = 1024;
constexpr uint16_t FRAME_PRE = 512;             // trigger index in a triggered frame

struct Frame {
	uint16_t s[FRAME_MAX];
	uint16_t n;          // valid samples
	uint16_t trig;       // trigger sample index
	float dt;            // seconds between samples
	bool triggered;      // false when Auto forced the capture
	bool roll;           // roll mode: newest sample is s[n-1], right aligned
	uint32_t total;      // roll mode: samples taken since the start (s[n-1] is total-1)
	Range range;
	Coupling coupling;
	int16_t zero;
};

/** Volts per ADC count for a range (front-end gain). */
float voltsPerCount(Range r);
