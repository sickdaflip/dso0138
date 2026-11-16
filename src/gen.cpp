// SPDX-License-Identifier: MIT
#include "gen.h"
#include <math.h>
#include <string.h>
#include "hal.h"
#include "measure.h"

namespace {

constexpr uint32_t TIMER_HZ = 72000000;
constexpr uint32_t SEG_HZ = 36000000;    // segment player tick (prescaler 2)
constexpr uint32_t MAX_CHUNK = 65535;    // ticks per timer period

// 1-2-5 frequency table, 1 Hz .. 1 MHz
const uint32_t FREQS[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000,
                          20000, 50000, 100000, 200000, 500000, 1000000};
constexpr uint8_t FREQ_COUNT = sizeof FREQS / sizeof FREQS[0];
constexpr uint8_t F_1K = 9;

const uint32_t BAUDS[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
const uint32_t BITRATES[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
const uint8_t GLITCH_US[] = {1, 2, 5, 10, 20, 50, 100};
const uint8_t SWEEP_S[] = {2, 5, 10, 20};
const char UART_TEXT[] = "Hello DSO138!\r\n";

struct ParamDef {
	uint8_t count;  // number of steps
	uint8_t def;    // default step
};

// Parameter definitions per mode: {param 0, param 1}
const ParamDef PARAMS[Gen::GEN_MODE_COUNT][2] = {
	{{0, 0}, {0, 0}},                       // test
	{{FREQ_COUNT, F_1K}, {19, 9}},          // square: frequency, duty 5..95 %
	{{4, 1}, {0, 0}},                       // sweep: duration
	{{7, 2}, {20, 4}},                      // burst: 1k..100k, 1..20 pulses
	{{7, 1}, {0, 0}},                       // glitch: width
	{{8, 3}, {0, 0}},                       // uart: baud
	{{9, 6}, {0, 0}},                       // prbs: bit rate
	{{21, 10}, {0, 0}},                     // servo: 0.5..2.5 ms
	{{0, 0}, {0, 0}},                       // ir: command, handled separately
	{{21, 10}, {0, 0}},                     // fan: 0..100 %
};

Gen::Mode cur = Gen::GEN_TEST;
uint8_t param[2] = {0, 0};
uint8_t irCommand = 0x45;

// Sweep state
uint32_t sweepStart = 0;
uint32_t sweepLast = 0;

// ---------------------------------------------------------------------------
// Hardware PWM
// ---------------------------------------------------------------------------
void stopTimer() {
	TIM3->CR1 = 0;
	TIM3->DIER = 0;
	NVIC_DisableIRQ(TIM3_IRQn);
	TIM3->SR = 0;
}

void pwm(float freq, float duty) {
	uint32_t ticks = uint32_t(TIMER_HZ / freq + 0.5f);
	if (ticks < 2) ticks = 2;
	uint32_t psc = (ticks - 1) / 65536;
	uint32_t arr = ticks / (psc + 1) - 1;
	TIM3->PSC = psc;
	TIM3->ARR = arr;
	TIM3->CCR2 = uint32_t((arr + 1) * duty + 0.5f);
}

void startPwm(float freq, float duty) {
	stopTimer();
	TIM3->CCMR1 = (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
	TIM3->CCER = TIM_CCER_CC2E;
	pwm(freq, duty);
	TIM3->EGR = TIM_EGR_UG;
	TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

// ---------------------------------------------------------------------------
// Segment player: each segment is `ticks` long (SEG_HZ) and high for the
// first `high` ticks. Pure levels may be longer than one timer period and are
// split into chunks.
// ---------------------------------------------------------------------------
struct Seg {
	uint32_t ticks;
	uint32_t high;
};

Seg pending{0, 0};
uint32_t step = 0;          // position inside the current pattern
uint16_t prbs = 0x7F;       // PRBS7 shift register

constexpr uint32_t us(uint32_t micro) { return micro * (SEG_HZ / 1000000); }

Seg level(bool high, uint32_t ticks) {
	return Seg{ticks, high ? ticks : 0};
}

/** Produces the next segment of the current pattern. */
Seg nextSegment() {
	switch (cur) {
	case Gen::GEN_BURST: {
		static const uint32_t BF[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000};
		uint32_t period = SEG_HZ / BF[param[0]];
		uint32_t count = param[1] + 1;
		// count pulses, then a pause of 3 bursts
		if (step < count) {
			step++;
			return Seg{period, period / 2};
		}
		step = 0;
		return level(false, period * count * 3);
	}

	case Gen::GEN_GLITCH: {
		const uint32_t period = SEG_HZ / 1000, half = period / 2;
		uint32_t w = us(GLITCH_US[param[0]]);
		uint32_t s = step++;
		if (s % 12 == 9) return Seg{half + half / 2, half};          // high, then low
		if (s % 12 == 10) return Seg{w, w};                           // the glitch
		if (s % 12 == 11) return level(false, half / 2 - w);          // rest of low
		return Seg{period, half};
	}

	case Gen::GEN_UART: {
		uint32_t bit = (SEG_HZ + BAUDS[param[0]] / 2) / BAUDS[param[0]];
		constexpr uint32_t CHARS = sizeof UART_TEXT - 1;
		uint32_t s = step++;
		if (s >= CHARS * 10) {
			step = 0;
			return level(true, SEG_HZ / 50);  // 20 ms idle
		}
		uint8_t c = uint8_t(UART_TEXT[s / 10]);
		uint8_t b = s % 10;
		bool v = b == 0 ? false : b == 9 ? true : (c >> (b - 1)) & 1;
		return level(v, bit);
	}

	case Gen::GEN_PRBS: {
		uint32_t bit = SEG_HZ / BITRATES[param[0]];
		uint16_t nb = ((prbs >> 6) ^ (prbs >> 5)) & 1;
		prbs = ((prbs << 1) | nb) & 0x7F;
		return level(nb, bit);
	}

	case Gen::GEN_IR: {
		// NEC: 9 ms burst, 4.5 ms space, 32 bits (562.5 us burst, then 562.5 us
		// or 1687.5 us space), final burst. A receiver
		// module outputs low during bursts.
		const uint32_t T = SEG_HZ / 1000000 * 5625 / 10;  // 562.5 us
		uint32_t frame[4] = {0x00, 0xFF, irCommand, uint8_t(~irCommand)};
		uint32_t s = step++;
		if (s == 0) return level(false, T * 16);
		if (s == 1) return level(true, T * 8);
		if (s < 2 + 64) {
			uint32_t i = (s - 2) / 2;
			bool one = (frame[i / 8] >> (i % 8)) & 1;
			return (s % 2 == 0) ? level(false, T) : level(true, one ? T * 3 : T);
		}
		if (s == 66) return level(false, T);
		step = 0;
		return level(true, SEG_HZ / 1000 * 40);  // 40 ms idle between frames
	}

	default:
		return level(false, us(1000));
	}
}

void loadSegment() {
	if (pending.ticks == 0) pending = nextSegment();
	uint32_t chunk = pending.ticks;
	uint32_t high = pending.high;
	if (chunk > MAX_CHUNK) {
		// only pure levels are split
		chunk = MAX_CHUNK;
		high = pending.high ? chunk : 0;
		pending.ticks -= chunk;
		if (pending.high) pending.high = pending.ticks;
	} else {
		pending.ticks = 0;
	}
	TIM3->ARR = chunk - 1;
	TIM3->CCR2 = high >= chunk ? 0xFFFF : high;
}

void startPlayer() {
	stopTimer();
	pending = Seg{0, 0};
	step = 0;
	prbs = 0x7F;
	TIM3->PSC = TIMER_HZ / SEG_HZ - 1;
	TIM3->CCMR1 = (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
	TIM3->CCER = TIM_CCER_CC2E;
	loadSegment();
	TIM3->EGR = TIM_EGR_UG;  // first segment becomes active
	TIM3->SR = 0;
	loadSegment();           // preload the second one
	TIM3->DIER = TIM_DIER_UIE;
	NVIC_SetPriority(TIM3_IRQn, 2);
	NVIC_EnableIRQ(TIM3_IRQn);
	TIM3->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void apply() {
	switch (cur) {
	case Gen::GEN_TEST: startPwm(1000, 0.5f); break;
	case Gen::GEN_SQUARE: startPwm(FREQS[param[0]], (param[1] + 1) * 0.05f); break;
	case Gen::GEN_SWEEP:
		startPwm(100, 0.5f);
		sweepStart = hal::millis();
		sweepLast = 0;
		break;
	case Gen::GEN_SERVO: startPwm(50, (0.5f + param[0] * 0.1f) / 20.0f); break;
	case Gen::GEN_FAN: startPwm(25000, param[0] * 0.05f); break;
	default: startPlayer(); break;
	}
}

}  // namespace

extern "C" void TIM3_IRQHandler() {
	if (TIM3->SR & TIM_SR_UIF) {
		TIM3->SR = ~TIM_SR_UIF;
		loadSegment();  // takes effect at the next update
	}
}

namespace Gen {

void begin() {
	hal::pinMode(GPIOA, 7, hal::PIN_AF_OUTPUT);
	setMode(GEN_TEST);
}

Mode mode() {
	return cur;
}

void setMode(Mode m) {
	cur = m;
	param[0] = PARAMS[m][0].def;
	param[1] = PARAMS[m][1].def;
	apply();
}

uint8_t paramCount() {
	if (cur == GEN_IR) return 1;
	return (PARAMS[cur][0].count ? 1 : 0) + (PARAMS[cur][1].count ? 1 : 0);
}

uint16_t paramSteps(uint8_t which) {
	if (which >= paramCount()) return 0;
	if (cur == GEN_IR) return 256;
	return PARAMS[cur][which].count;
}

uint16_t paramValue(uint8_t which) {
	if (cur == GEN_IR) return irCommand;
	return which < 2 ? param[which] : 0;
}

void setParam(uint8_t which, uint16_t value) {
	uint16_t n = paramSteps(which);
	if (!n) return;
	if (value >= n) value = n - 1;
	if (cur == GEN_IR) irCommand = uint8_t(value);
	else param[which] = uint8_t(value);
	apply();
}

void adjust(uint8_t which, int8_t dir) {
	if (cur == GEN_IR && which == 0) {
		irCommand = uint8_t(irCommand + dir);
	} else {
		uint8_t n = PARAMS[cur][which].count;
		if (!n) return;
		int16_t v = int16_t(param[which]) + dir;
		if (v < 0 || v >= n) return;
		param[which] = uint8_t(v);
	}
	apply();
}

void describe(uint8_t which, char *out, size_t len) {
	out[0] = 0;
	auto num = [&](uint32_t v, const char *unit) {
		char tmp[12];
		uint8_t n = 0;
		do { tmp[n++] = char('0' + v % 10); v /= 10; } while (v);
		size_t p = 0;
		while (n && p < len - 1) out[p++] = tmp[--n];
		out[p] = 0;
		strncat(out, unit, len - strlen(out) - 1);
	};
	switch (cur) {
	case GEN_SQUARE:
		if (which == 0) Measure::formatDiv(out, len, float(FREQS[param[0]]), "Hz");
		else num((param[1] + 1) * 5, " %");
		break;
	case GEN_SWEEP: num(SWEEP_S[param[0]], " s"); break;
	case GEN_BURST:
		if (which == 0) {
			static const uint32_t BF[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000};
			Measure::formatDiv(out, len, float(BF[param[0]]), "Hz");
		} else {
			num(param[1] + 1, "\xC3\x97");  // multiplication sign
		}
		break;
	case GEN_GLITCH: num(GLITCH_US[param[0]], " \xC2\xB5s"); break;
	case GEN_UART: num(BAUDS[param[0]], " Bd"); break;
	case GEN_PRBS: Measure::formatDiv(out, len, float(BITRATES[param[0]]), "bit/s"); break;
	case GEN_SERVO: {
		uint8_t tenths = 5 + param[0];
		char b[8] = {char('0' + tenths / 10), '.', char('0' + tenths % 10), ' ', 'm', 's', 0};
		strncpy(out, b, len - 1);
		out[len - 1] = 0;
		break;
	}
	case GEN_IR: {
		static const char HEX[] = "0123456789ABCDEF";
		char b[8] = {'0', 'x', HEX[irCommand >> 4], HEX[irCommand & 15], 0};
		strncpy(out, b, len - 1);
		out[len - 1] = 0;
		break;
	}
	case GEN_FAN: num(param[0] * 5, " %"); break;
	default: break;
	}
}

void poll(uint32_t now) {
	if (cur != GEN_SWEEP || now - sweepLast < 10) return;
	sweepLast = now;
	float period = SWEEP_S[param[0]] * 1000.0f;
	float t = fmodf(float(now - sweepStart), period) / period;
	pwm(100.0f * powf(1000.0f, t), 0.5f);  // 100 Hz .. 100 kHz
}

}  // namespace Gen
