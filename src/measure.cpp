// SPDX-License-Identifier: MIT
#include "measure.h"
#include <math.h>
#include <string.h>

namespace {

void appendStr(char *&p, char *end, const char *s) {
	while (*s && p < end - 1) *p++ = *s++;
	*p = 0;
}

void appendUInt(char *&p, char *end, uint32_t v) {
	char tmp[11];
	uint8_t n = 0;
	do {
		tmp[n++] = char('0' + v % 10);
		v /= 10;
	} while (v);
	while (n && p < end - 1) *p++ = tmp[--n];
	*p = 0;
}

struct Prefix {
	float scale;
	const char *sym;
};

const Prefix PREFIXES[] = {
	{1e6f, "M"}, {1e3f, "k"}, {1.0f, ""}, {1e-3f, "m"}, {1e-6f, "µ"}, {1e-9f, "n"},
};

/** Writes `value` with `sig` significant digits plus prefix and unit. */
void formatSig(char *out, size_t len, float value, const char *unit, uint8_t sig, bool trimZeros) {
	char *p = out;
	char *end = out + len;
	*p = 0;
	if (value < 0) {
		appendStr(p, end, "-");
		value = -value;
	}
	const Prefix *pre = &PREFIXES[2];
	if (value != 0.0f) {
		for (const Prefix &x : PREFIXES) {
			if (value >= x.scale * 0.9995f) { pre = &x; break; }
			pre = &x;
		}
	}
	float v = value / pre->scale;
	// digits before the decimal point
	uint8_t intDigits = v >= 99.95f ? 3 : v >= 9.995f ? 2 : 1;
	int8_t decimals = int8_t(sig) - intDigits;
	if (decimals < 0) decimals = 0;
	uint32_t mul = 1;
	for (int8_t i = 0; i < decimals; i++) mul *= 10;
	uint32_t scaled = uint32_t(v * mul + 0.5f);
	uint32_t ip = scaled / mul;
	uint32_t fp = scaled % mul;
	if (trimZeros) {
		while (decimals > 0 && fp % 10 == 0) {
			fp /= 10;
			mul /= 10;
			decimals--;
		}
	}
	appendUInt(p, end, ip);
	if (decimals > 0) {
		appendStr(p, end, ".");
		for (uint32_t d = mul / 10; d > 0; d /= 10) {
			char c[2] = {char('0' + (fp / d) % 10), 0};
			appendStr(p, end, c);
		}
	}
	appendStr(p, end, " ");
	appendStr(p, end, pre->sym);
	appendStr(p, end, unit);
}

}  // namespace

namespace Measure {

void format(char *out, size_t len, float value, const char *unit) {
	formatSig(out, len, value, unit, 3, false);
}

void formatDiv(char *out, size_t len, float value, const char *unit) {
	formatSig(out, len, value, unit, 3, true);
}

void compute(const Frame &f, Measurements &m) {
	memset(&m, 0, sizeof m);
	if (f.n < 2) return;
	const float vpc = voltsPerCount(f.range);
	uint16_t lo = 0xFFFF, hi = 0;
	int64_t sum = 0;
	uint64_t sq = 0;
	for (uint16_t i = 0; i < f.n; i++) {
		uint16_t s = f.s[i];
		if (s < lo) lo = s;
		if (s > hi) hi = s;
		int32_t d = int32_t(s) - f.zero;
		sum += d;
		sq += uint64_t(int64_t(d) * d);
	}
	m.clipped = lo <= 4 || hi >= 4091;
	m.vmax = (int32_t(hi) - f.zero) * vpc;
	m.vmin = (int32_t(lo) - f.zero) * vpc;
	m.vpp = (hi - lo) * vpc;
	m.vavg = float(sum) / f.n * vpc;
	m.vrms = sqrtf(float(sq) / f.n) * vpc;

	// Frequency from rising crossings of the midpoint with hysteresis.
	int32_t span = hi - lo;
	if (span < 24) return;
	int32_t mid = (int32_t(hi) + lo) / 2;
	int32_t hyst = span / 8;
	bool armed = f.s[0] < mid - hyst;
	float first = -1, last = -1;
	uint16_t edges = 0;
	for (uint16_t i = 1; i < f.n; i++) {
		int32_t s = f.s[i];
		if (s < mid - hyst) armed = true;
		if (armed && s >= mid && f.s[i - 1] < mid) {
			// sub-sample position of the crossing
			float a = f.s[i - 1], b = s;
			float pos = (i - 1) + (mid - a) / (b - a);
			if (first < 0) first = pos;
			last = pos;
			edges++;
			armed = false;
		}
	}
	if (edges >= 2 && last > first) {
		float period = (last - first) / (edges - 1) * f.dt;
		m.freq = 1.0f / period;
		// duty over whole periods only
		uint16_t a = uint16_t(first + 1), b = uint16_t(last);
		if (b > a) {
			uint32_t high = 0;
			for (uint16_t i = a; i <= b; i++) {
				if (f.s[i] >= mid) high++;
			}
			m.duty = float(high) / float(b - a + 1);
		}
	}
}

}  // namespace Measure
