// SPDX-License-Identifier: MIT
#include "storage.h"
#include <string.h>
#include "board.h"

namespace {

constexpr uint32_t PAGE_ADDR = 0x0800FC00;
constexpr uint32_t PAGE_SIZE = 1024;
constexpr uint16_t MAGIC = 0xD138;
constexpr uint16_t VERSION = 1;

struct Record {
	uint16_t magic;
	uint16_t version;
	Settings settings;
	uint16_t crc;
	uint16_t pad;
};
static_assert(sizeof(Record) % 4 == 0, "record must be word aligned");
constexpr uint32_t SLOTS = PAGE_SIZE / sizeof(Record);

uint16_t crc16(const uint8_t *p, uint32_t n) {
	uint16_t crc = 0xFFFF;
	while (n--) {
		crc ^= *p++;
		for (uint8_t i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
	}
	return crc;
}

const Record *slot(uint32_t i) {
	return reinterpret_cast<const Record *>(PAGE_ADDR + i * sizeof(Record));
}

bool slotEmpty(uint32_t i) {
	const uint32_t *w = reinterpret_cast<const uint32_t *>(slot(i));
	for (uint32_t k = 0; k < sizeof(Record) / 4; k++) {
		if (w[k] != 0xFFFFFFFF) return false;
	}
	return true;
}

bool slotValid(uint32_t i) {
	const Record *r = slot(i);
	return r->magic == MAGIC && r->version == VERSION &&
	       r->crc == crc16(reinterpret_cast<const uint8_t *>(&r->settings), sizeof(Settings));
}

void unlock() {
	if (FLASH->CR & FLASH_CR_LOCK) {
		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xCDEF89AB;
	}
}

void waitBusy() {
	while (FLASH->SR & FLASH_SR_BSY) {}
}

void erasePage() {
	unlock();
	waitBusy();
	FLASH->CR |= FLASH_CR_PER;
	FLASH->AR = PAGE_ADDR;
	FLASH->CR |= FLASH_CR_STRT;
	waitBusy();
	FLASH->CR &= ~FLASH_CR_PER;
	FLASH->CR |= FLASH_CR_LOCK;
}

void program(uint32_t addr, const void *data, uint32_t len) {
	unlock();
	const uint16_t *src = static_cast<const uint16_t *>(data);
	FLASH->CR |= FLASH_CR_PG;
	for (uint32_t i = 0; i < len / 2; i++) {
		*reinterpret_cast<volatile uint16_t *>(addr + i * 2) = src[i];
		waitBusy();
	}
	FLASH->CR &= ~FLASH_CR_PG;
	FLASH->CR |= FLASH_CR_LOCK;
}

}  // namespace

namespace Storage {

bool load(Settings &s) {
	for (int32_t i = SLOTS - 1; i >= 0; i--) {
		if (slotValid(i)) {
			memcpy(&s, &slot(i)->settings, sizeof s);
			return true;
		}
	}
	settingsDefaults(s);
	return false;
}

void save(const Settings &s) {
	int32_t last = -1;
	for (int32_t i = SLOTS - 1; i >= 0; i--) {
		if (slotValid(i)) { last = i; break; }
	}
	if (last >= 0 && memcmp(&slot(last)->settings, &s, sizeof s) == 0) return;

	Record r;
	memset(&r, 0, sizeof r);
	r.magic = MAGIC;
	r.version = VERSION;
	r.settings = s;
	r.crc = crc16(reinterpret_cast<const uint8_t *>(&s), sizeof s);

	uint32_t next = last + 1;
	if (next >= SLOTS || !slotEmpty(next)) {
		erasePage();
		next = 0;
	}
	program(PAGE_ADDR + next * sizeof(Record), &r, sizeof r);
}

void erase() {
	erasePage();
}

}  // namespace Storage
