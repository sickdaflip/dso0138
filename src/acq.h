// SPDX-License-Identifier: MIT
/**
 * Signal acquisition.
 *
 *  - Timer mode (50 us/div and slower): TIM2 CC2 triggers ADC1, DMA1 channel 1
 *    writes into a circular buffer, 25 samples per division. The main loop
 *    scans new samples for the trigger condition and stops the capture once
 *    the post-trigger part is complete.
 *  - Fast mode (10 and 20 us/div): ADC1 and ADC2 in fast interleaved dual
 *    mode, about 1.71 MS/s. One burst is captured and searched for the trigger.
 *  - Roll mode (Auto, 100 ms/div and slower): continuous, untriggered.
 *
 * The slide switches are read on ADC2 between captures.
 */
#pragma once
#include "model.h"

namespace Acq {

enum State : uint8_t {
	ACQ_IDLE,     // stopped, last frame kept
	ACQ_PREFILL,  // collecting pre-trigger samples
	ACQ_ARMED,    // waiting for the trigger
	ACQ_POST,     // triggered, collecting post-trigger samples
	ACQ_ROLL,     // roll mode, continuous
};

struct Switches {
	Range range;
	Coupling coupling;
};

void begin();

/** Reads SEN1, SEN2 and CPL. Only valid when not capturing on ADC2. */
Switches readSwitches();

/**
 * Starts a capture with the given settings. `level` is the absolute ADC
 * trigger level. Restarts if one is running.
 */
void start(const Settings &s, int16_t level, Range r, Coupling c, int16_t zero);
void stop();

/**
 * Advances the capture. Returns true when a new frame is ready in `frame()`.
 * In roll mode it returns true whenever new samples arrived.
 */
bool poll(uint32_t now);

State state();
const Frame &frame();

/** Mean of the last frame (for zero calibration). */
int16_t frameMean();

}  // namespace Acq
