// SPDX-License-Identifier: MIT
/**
 * Debounced front panel buttons with long press and auto repeat on +/-.
 */
#pragma once
#include <stdint.h>

enum Key : uint8_t { KEY_NONE, KEY_SEL, KEY_MINUS, KEY_PLUS, KEY_OK, KEY_OK_LONG, KEY_SEL_LONG };

namespace Buttons {

void begin();

/** Call every millisecond or faster; returns at most one event per call. */
Key poll(uint32_t now);

/** Number of auto repeats of the key being held (for acceleration). */
uint16_t repeatCount();


}  // namespace Buttons
