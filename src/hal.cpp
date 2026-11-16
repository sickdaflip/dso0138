// SPDX-License-Identifier: MIT
#include "hal.h"
#include "buttons.h"

namespace {

volatile uint32_t ticks = 0;

// USART1 receive ring buffer
constexpr uint16_t RX_SIZE = 128;
volatile uint8_t rxBuf[RX_SIZE];
volatile uint16_t rxHead = 0, rxTail = 0;

// USART1 transmit ring buffer: a whole waveform frame fits, so streaming
// does not block the main loop.
constexpr uint16_t TX_SIZE = 6144;
volatile uint8_t txBuf[TX_SIZE];
volatile uint16_t txHead = 0, txTail = 0;

}  // namespace

extern "C" void SysTick_Handler() {
	ticks++;
	Buttons::tick(ticks);
}

extern "C" void USART1_IRQHandler() {
	if (USART1->SR & USART_SR_RXNE) {
		uint8_t c = USART1->DR;
		uint16_t n = (rxHead + 1) % RX_SIZE;
		if (n != rxTail) {
			rxBuf[rxHead] = c;
			rxHead = n;
		}
	}
	if (USART1->SR & USART_SR_ORE) (void)USART1->DR;
	if ((USART1->CR1 & USART_CR1_TXEIE) && (USART1->SR & USART_SR_TXE)) {
		if (txHead == txTail) {
			USART1->CR1 &= ~USART_CR1_TXEIE;
		} else {
			USART1->DR = txBuf[txTail];
			txTail = (txTail + 1) % TX_SIZE;
		}
	}
}

namespace hal {

void init() {
	// Flash: 2 wait states and prefetch for 72 MHz
	FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

	// HSE on
	RCC->CR |= RCC_CR_HSEON;
	for (uint32_t i = 0; i < 500000 && !(RCC->CR & RCC_CR_HSERDY); i++) {}

	if (RCC->CR & RCC_CR_HSERDY) {
		// PLL = HSE x 9 = 72 MHz; AHB 72, APB1 36, APB2 72, ADC 72/6 = 12 MHz
		RCC->CFGR = RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9 | RCC_CFGR_PPRE1_DIV2 |
		            RCC_CFGR_PPRE2_DIV1 | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_ADCPRE_DIV6;
	} else {
		// Fallback: HSI/2 x 16 = 64 MHz
		RCC->CFGR = RCC_CFGR_PLLMULL16 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_ADCPRE_DIV6;
	}
	RCC->CR |= RCC_CR_PLLON;
	while (!(RCC->CR & RCC_CR_PLLRDY)) {}
	RCC->CFGR |= RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
	SystemCoreClockUpdate();

	// Peripheral clocks
	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
	                RCC_APB2ENR_IOPCEN | RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN |
	                RCC_APB2ENR_USART1EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM4EN;
	RCC->AHBENR |= RCC_AHBENR_DMA1EN;

	// JTAG off, SWD on: frees PA15, PB3, PB4
	AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

	SysTick_Config(SystemCoreClock / 1000);

	pinMode(LED_PORT, LED_PIN, PIN_OUTPUT);
	led(false);
}

uint32_t millis() {
	return ticks;
}

void delayMs(uint32_t ms) {
	uint32_t start = ticks;
	while (ticks - start < ms) {}
}

void delayUs(uint32_t us) {
	// SysTick counts down from LOAD at the core clock
	const uint32_t perUs = SystemCoreClock / 1000000;
	uint32_t total = us * perUs;
	uint32_t last = SysTick->VAL;
	while (total) {
		uint32_t now = SysTick->VAL;
		uint32_t elapsed = last >= now ? last - now : last + SysTick->LOAD + 1 - now;
		last = now;
		total = elapsed >= total ? 0 : total - elapsed;
	}
}

void pinMode(GPIO_TypeDef *port, uint8_t pin, PinMode mode) {
	volatile uint32_t *reg = pin < 8 ? &port->CRL : &port->CRH;
	uint8_t shift = (pin & 7) * 4;
	*reg = (*reg & ~(0xFu << shift)) | (uint32_t(mode) << shift);
}

void startPwm() {
	// TIM4: 72 MHz timer clock, 20 kHz period. CH3 = PB8 trigger reference,
	// CH4 = PB9 charge pump for the analog negative rail.
	pinMode(GPIOB, 8, PIN_AF_OUTPUT);
	pinMode(GPIOB, 9, PIN_AF_OUTPUT);
	TIM4->PSC = 0;
	TIM4->ARR = 72000000 / VGEN_FREQ_HZ - 1;
	TIM4->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
	              (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
	TIM4->CCR3 = (TIM4->ARR + 1) / 2;
	TIM4->CCR4 = (TIM4->ARR + 1) * VGEN_DUTY_PERMILLE / 1000;
	TIM4->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;
	TIM4->EGR = TIM_EGR_UG;
	TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

	// The test signal on PA7 (TIM3) is handled by the signal generator.
}

void led(bool on) {
	if (on) pinHigh(LED_PORT, LED_PIN);
	else pinLow(LED_PORT, LED_PIN);
}

constexpr uint16_t BOOT_MAGIC = 0xB007;

void enterBootloader() {
	serialFlush();  // let the last reply leave the UART
	RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
	PWR->CR |= PWR_CR_DBP;
	BKP->DR1 = BOOT_MAGIC;
	NVIC_SystemReset();
}

void bootCheck() {
	RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
	if (BKP->DR1 != BOOT_MAGIC) {
		RCC->APB1ENR &= ~(RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN);
		return;
	}
	PWR->CR |= PWR_CR_DBP;
	BKP->DR1 = 0;
	PWR->CR &= ~PWR_CR_DBP;
	RCC->APB1ENR &= ~(RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN);

	// The chip is in its reset state (HSI, no peripherals): start the ROM
	// bootloader exactly like the hardware does with BOOT0 high.
	constexpr uint32_t SYSMEM = 0x1FFFF000;
	const uint32_t sp = *reinterpret_cast<const volatile uint32_t *>(SYSMEM);
	const uint32_t pc = *reinterpret_cast<const volatile uint32_t *>(SYSMEM + 4);
	SCB->VTOR = SYSMEM;
	__set_MSP(sp);
	reinterpret_cast<void (*)()>(pc)();
	for (;;) {}
}

void serialInit(uint32_t baud) {
	pinMode(GPIOA, 9, PIN_AF_OUTPUT);
	pinMode(GPIOA, 10, PIN_INPUT_FLOAT);
	USART1->BRR = (SystemCoreClock + baud / 2) / baud;  // APB2 = core clock
	USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
	NVIC_SetPriority(USART1_IRQn, 3);
	NVIC_EnableIRQ(USART1_IRQn);
}

void serialPutc(char c) {
	uint16_t n = (txHead + 1) % TX_SIZE;
	while (n == txTail) {}  // buffer full: wait for the interrupt to drain it
	txBuf[txHead] = uint8_t(c);
	txHead = n;
	USART1->CR1 |= USART_CR1_TXEIE;
}

void serialFlush() {
	while (txHead != txTail) {}
	while (!(USART1->SR & USART_SR_TC)) {}
}

void serialWrite(const char *s) {
	while (*s) serialPutc(*s++);
}

void serialWrite(const uint8_t *data, size_t len) {
	while (len--) serialPutc(char(*data++));
}

int serialRead() {
	if (rxHead == rxTail) return -1;
	uint8_t c = rxBuf[rxTail];
	rxTail = (rxTail + 1) % RX_SIZE;
	return c;
}

}  // namespace hal
