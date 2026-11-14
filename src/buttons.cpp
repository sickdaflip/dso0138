// SPDX-License-Identifier: MIT
#include "buttons.h"
#include "hal.h"

namespace {

constexpr uint8_t PINS[4] = {BTN_SEL_PIN, BTN_MINUS_PIN, BTN_PLUS_PIN, BTN_OK_PIN};
constexpr Key SHORT[4] = {KEY_SEL, KEY_MINUS, KEY_PLUS, KEY_OK};
constexpr uint16_t DEBOUNCE_MS = 25;
constexpr uint16_t LONG_MS = 700;
constexpr uint16_t REPEAT_DELAY_MS = 400;
constexpr uint16_t REPEAT_MS = 70;

struct State {
	bool down;
	bool handled;      // long press or repeat already fired
	uint32_t changed;  // last raw change
	uint32_t pressed;  // debounced press time
	uint32_t lastRepeat;
	bool raw;
};

State st[4];
uint16_t repeats = 0;

}  // namespace

namespace Buttons {

void begin() {
	for (uint8_t i = 0; i < 4; i++) {
		hal::pinMode(GPIOB, PINS[i], hal::PIN_INPUT_PULL);
		hal::pinHigh(GPIOB, PINS[i]);  // pull-up
		st[i] = State{};
	}
}

uint16_t repeatCount() {
	return repeats;
}

Key poll(uint32_t now) {
	for (uint8_t i = 0; i < 4; i++) {
		State &s = st[i];
		bool raw = !hal::pinRead(GPIOB, PINS[i]);
		if (raw != s.raw) {
			s.raw = raw;
			s.changed = now;
		}
		if (now - s.changed < DEBOUNCE_MS) continue;

		if (raw && !s.down) {
			s.down = true;
			s.handled = false;
			s.pressed = now;
			s.lastRepeat = now;
			repeats = 0;
			// +/- fire on press for a snappy feel
			if (i == 1 || i == 2) return SHORT[i];
		} else if (!raw && s.down) {
			s.down = false;
			if (!s.handled && (i == 0 || i == 3)) return SHORT[i];
		} else if (raw && s.down && !s.handled) {
			if ((i == 0 || i == 3) && now - s.pressed >= LONG_MS) {
				s.handled = true;
				return i == 3 ? KEY_OK_LONG : KEY_SEL_LONG;
			}
		}
		if (raw && s.down && (i == 1 || i == 2) && now - s.pressed >= REPEAT_DELAY_MS &&
		    now - s.lastRepeat >= REPEAT_MS) {
			s.lastRepeat = now;
			if (repeats < 0xFFFF) repeats++;
			return SHORT[i];
		}
	}
	return KEY_NONE;
}

}  // namespace Buttons
