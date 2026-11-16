// SPDX-License-Identifier: MIT
#include "buttons.h"
#include "hal.h"

/*
 * The buttons are sampled every millisecond from the SysTick interrupt, so
 * debouncing and press timing do not depend on how long the main loop is
 * busy (screen redraws, serial transfers). An integrator per button has to
 * count all the way up or down before the state flips, which filters both
 * contact bounce and short glitches. Events go into a small queue.
 */

namespace {

constexpr uint8_t PINS[4] = {BTN_SEL_PIN, BTN_MINUS_PIN, BTN_PLUS_PIN, BTN_OK_PIN};
constexpr Key SHORT[4] = {KEY_SEL, KEY_MINUS, KEY_PLUS, KEY_OK};
constexpr uint8_t INTEGRATE_MS = 20;
constexpr uint16_t LONG_MS = 700;
constexpr uint16_t REPEAT_DELAY_MS = 450;
constexpr uint16_t REPEAT_MS = 80;

struct State {
	uint8_t level;      // integrator 0..INTEGRATE_MS
	bool down;          // debounced state
	bool handled;       // long press or repeat already fired
	uint32_t pressed;   // debounced press time
	uint32_t lastRepeat;
};

State st[4];
volatile uint16_t repeats = 0;
bool ready = false;

constexpr uint8_t QUEUE = 8;
volatile Key queue[QUEUE];
volatile uint8_t head = 0, tail = 0;

void push(Key k) {
	uint8_t n = (head + 1) % QUEUE;
	if (n == tail) return;  // full: drop
	queue[head] = k;
	head = n;
}

}  // namespace

namespace Buttons {

void begin() {
	for (uint8_t i = 0; i < 4; i++) {
		hal::pinMode(GPIOB, PINS[i], hal::PIN_INPUT_PULL);
		hal::pinHigh(GPIOB, PINS[i]);  // pull-up
		st[i] = State{};
	}
	head = tail = 0;
	ready = true;
}

void tick(uint32_t now) {
	if (!ready) return;
	for (uint8_t i = 0; i < 4; i++) {
		State &s = st[i];
		bool raw = !hal::pinRead(GPIOB, PINS[i]);
		if (raw && s.level < INTEGRATE_MS) s.level++;
		else if (!raw && s.level > 0) s.level--;

		bool plusMinus = i == 1 || i == 2;
		if (!s.down && s.level == INTEGRATE_MS) {
			s.down = true;
			s.handled = false;
			s.pressed = now;
			s.lastRepeat = now;
			repeats = 0;
			if (plusMinus) push(SHORT[i]);  // +/- react on press
		} else if (s.down && s.level == 0) {
			s.down = false;
			if (!s.handled && !plusMinus) push(SHORT[i]);
		} else if (s.down) {
			if (!plusMinus && !s.handled && now - s.pressed >= LONG_MS) {
				s.handled = true;
				push(i == 3 ? KEY_OK_LONG : KEY_SEL_LONG);
			}
			if (plusMinus && now - s.pressed >= REPEAT_DELAY_MS && now - s.lastRepeat >= REPEAT_MS) {
				s.lastRepeat = now;
				if (repeats < 0xFFFF) repeats++;
				push(SHORT[i]);
			}
		}
	}
}

Key poll(uint32_t) {
	if (head == tail) return KEY_NONE;
	Key k = queue[tail];
	tail = (tail + 1) % QUEUE;
	return k;
}

uint16_t repeatCount() {
	return repeats;
}

}  // namespace Buttons
