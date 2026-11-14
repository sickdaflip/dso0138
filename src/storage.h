// SPDX-License-Identifier: MIT
/**
 * Settings persistence in the last 1 KiB flash page (0x0800FC00).
 * Records are appended until the page is full, then the page is erased,
 * which keeps flash wear low. Each record carries a CRC-16.
 */
#pragma once
#include "model.h"

namespace Storage {

/** Loads the newest valid record; falls back to defaults. Returns true if found. */
bool load(Settings &s);

/** Writes a record if it differs from the last stored one. */
void save(const Settings &s);

/** Erases the settings page. */
void erase();

}  // namespace Storage
