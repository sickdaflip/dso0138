// SPDX-License-Identifier: MIT
/**
 * Automatic measurements on a captured frame and value formatting.
 */
#pragma once
#include <stddef.h>
#include "model.h"

struct Measurements {
	float vmax, vmin, vpp, vavg, vrms;
	float freq;      // Hz, 0 if no periodic signal found
	float duty;      // 0..1, valid with freq
	bool clipped;    // samples at the ADC limits
};

namespace Measure {

void compute(const Frame &f, Measurements &m);

/** Formats with 3 significant digits and an SI prefix, e.g. "1.23 kHz". */
void format(char *out, size_t len, float value, const char *unit);

/** Formats a timebase or volts-per-division value, e.g. "500 µs" or "50 mV". */
void formatDiv(char *out, size_t len, float value, const char *unit);

}  // namespace Measure
