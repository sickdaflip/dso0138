// SPDX-License-Identifier: MIT
/**
 * Signal generator on the test signal pin (PA7, TIM3 CH2, 0/3.3 V).
 *
 * Continuous signals (test, square, sweep, servo, fan) are plain hardware
 * PWM. Patterned signals (burst, glitch, UART, PRBS, IR) are played by the
 * TIM3 update interrupt: every timer period is one segment (length and high
 * time), computed on the fly, so no pattern buffers are needed.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Gen {

enum Mode : uint8_t {
	GEN_TEST,     // 1 kHz, 50 % (stock calibration signal)
	GEN_SQUARE,   // frequency, duty
	GEN_SWEEP,    // 100 Hz .. 100 kHz logarithmic, duration
	GEN_BURST,    // pulse frequency, pulse count
	GEN_GLITCH,   // 1 kHz square with a short extra pulse every 10th period
	GEN_UART,     // "Hello DSO138!\r\n", 8N1, baud rate
	GEN_PRBS,     // pseudo random bit sequence (PRBS7), bit rate
	GEN_SERVO,    // 50 Hz, pulse width
	GEN_IR,       // NEC remote, IR receiver output (active low), command
	GEN_FAN,      // 25 kHz PWM, duty
	GEN_MODE_COUNT
};

void begin();

Mode mode();
void setMode(Mode m);

/** Number of adjustable parameters of the current mode (0..2). */
uint8_t paramCount();

/** Steps parameter `which` by `dir` (+1/-1) and restarts the output. */
void adjust(uint8_t which, int8_t dir);

/** Formats the value of parameter `which`, e.g. "9600 Bd". */
void describe(uint8_t which, char *out, size_t len);

/** Call from the main loop; updates the sweep. */
void poll(uint32_t now);

}  // namespace Gen
