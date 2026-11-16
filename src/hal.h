// SPDX-License-Identifier: MIT
/**
 * Minimal hardware abstraction: clock tree, SysTick time base, GPIO helpers,
 * PWM outputs and USART1.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "board.h"

namespace hal {

/** 72 MHz from the 8 MHz crystal, APB1 36 MHz, ADC 12 MHz, SysTick 1 kHz. */
void init();

uint32_t millis();
void delayMs(uint32_t ms);
void delayUs(uint32_t us);

enum PinMode : uint8_t {
	PIN_ANALOG = 0x0,
	PIN_INPUT_FLOAT = 0x4,
	PIN_INPUT_PULL = 0x8,      // pull direction set via ODR
	PIN_OUTPUT = 0x3,          // push-pull 50 MHz
	PIN_OUTPUT_2MHZ = 0x2,     // push-pull 2 MHz (PC13..PC15)
	PIN_AF_OUTPUT = 0xB,       // alternate function push-pull 50 MHz
};

void pinMode(GPIO_TypeDef *port, uint8_t pin, PinMode mode);

static inline void pinHigh(GPIO_TypeDef *port, uint8_t pin) { port->BSRR = 1u << pin; }
static inline void pinLow(GPIO_TypeDef *port, uint8_t pin) { port->BRR = 1u << pin; }
static inline bool pinRead(GPIO_TypeDef *port, uint8_t pin) { return port->IDR & (1u << pin); }

/** Starts the charge pump, trigger reference and 1 kHz test signal. */
void startPwm();

void led(bool on);

/**
 * Restarts into the STM32 system memory bootloader (USART1, 8E1, autobaud),
 * as if BOOT0 were high. A flag in backup register DR1 survives the system
 * reset; bootCheck() then jumps to the bootloader while the chip is still in
 * its reset state. Does not return.
 */
[[noreturn]] void enterBootloader();

/** Must be the first call in main(), before any clock or GPIO setup. */
void bootCheck();

// USART1 -------------------------------------------------------------------
void serialInit(uint32_t baud);
void serialWrite(const char *s);
void serialWrite(const uint8_t *data, size_t len);
void serialPutc(char c);
/** Returns -1 if nothing was received. */
int serialRead();

}  // namespace hal
