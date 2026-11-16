// SPDX-License-Identifier: MIT
#include "acq.h"
#include "hal.h"

namespace {

constexpr uint16_t RING = 2048;                       // power of two
constexpr uint16_t POST = FRAME_MAX - FRAME_PRE;
constexpr int16_t HYST = 8;                           // trigger hysteresis (counts)
constexpr float FAST_DT = 7.0f / 12e6f;               // interleaved sample spacing
constexpr uint32_t ROLL_REFRESH_MS = 40;

// ADC1 regular external trigger selections
constexpr uint32_t EXTSEL_TIM2_CC2 = 3u << ADC_CR2_EXTSEL_Pos;
constexpr uint32_t EXTSEL_SWSTART = 7u << ADC_CR2_EXTSEL_Pos;

union {
	uint16_t s[RING];
	uint32_t w[RING / 2];
} ring;

Frame fr;
volatile uint32_t wraps = 0;

Acq::State st = Acq::ACQ_IDLE;
bool fast = false;
bool roll = false;
TrigMode mode = TRIG_AUTO;
TrigEdge edge = EDGE_RISING;
int16_t level = 2048;
float dt = 0;
uint32_t scanned = 0;      // cumulative index of the next sample to check
uint32_t trigAt = 0;       // cumulative index of the trigger sample
bool armed = false;
uint32_t holdoffSamples = 0;
uint32_t lastActive = 0;   // last sample index on the trigger's far side
bool forced = false;
uint32_t armedSince = 0;
uint32_t autoTimeoutMs = 100;
uint32_t lastRoll = 0;
Acq::Switches lastSw{RNG_1V, CPL_DC};

void setCr2(ADC_TypeDef *adc, uint32_t v) {
	// Writing ADON=1 with no other change would start a conversion, so every
	// configuration write differs from the previous value in EXTTRIG.
	adc->CR2 = (v | ADC_CR2_ADON) & ~ADC_CR2_EXTTRIG;
	adc->CR2 = v | ADC_CR2_ADON | ADC_CR2_EXTTRIG;
}

void calibrate(ADC_TypeDef *adc) {
	adc->CR2 = ADC_CR2_ADON;
	hal::delayUs(10);
	adc->CR2 |= ADC_CR2_RSTCAL;
	while (adc->CR2 & ADC_CR2_RSTCAL) {}
	adc->CR2 |= ADC_CR2_CAL;
	while (adc->CR2 & ADC_CR2_CAL) {}
	setCr2(adc, EXTSEL_SWSTART);
}

void haltHw() {
	TIM2->CR1 = 0;
	DMA1_Channel1->CCR = 0;
	ADC1->CR2 &= ~ADC_CR2_CONT;
	ADC2->CR2 &= ~ADC_CR2_CONT;
	hal::delayUs(3);  // let a running conversion finish
	ADC1->CR1 &= ~ADC_CR1_DUALMOD;
	setCr2(ADC1, EXTSEL_SWSTART);
	setCr2(ADC2, EXTSEL_SWSTART);
	(void)ADC1->DR;
	(void)ADC2->DR;
}

uint32_t written() {
	uint32_t w1, w2, pos;
	do {
		w1 = wraps;
		pos = RING - DMA1_Channel1->CNDTR;
		w2 = wraps;
	} while (w1 != w2);
	if (pos >= RING) pos = 0;
	return w1 * RING + pos;
}

uint16_t sampleTime(float dts) {
	// ADC clock 12 MHz, conversion = sample time + 12.5 cycles
	if (dts < 5e-6f) return 1;   // 7.5 cycles
	if (dts < 10e-6f) return 3;  // 28.5 cycles
	return 5;                    // 55.5 cycles
}

void startTimerMode() {
	ADC1->SMPR2 = (ADC1->SMPR2 & ~ADC_SMPR2_SMP0) | (sampleTime(dt) << ADC_SMPR2_SMP0_Pos);
	ADC1->SQR1 = 0;
	ADC1->SQR3 = ADC_CH_SIGNAL;

	DMA1_Channel1->CPAR = uint32_t(&ADC1->DR);
	DMA1_Channel1->CMAR = uint32_t(ring.s);
	DMA1_Channel1->CNDTR = RING;
	DMA1->IFCR = DMA_IFCR_CGIF1;
	DMA1_Channel1->CCR = DMA_CCR_PL_1 | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0 | DMA_CCR_MINC |
	                     DMA_CCR_CIRC | DMA_CCR_TCIE | DMA_CCR_EN;
	setCr2(ADC1, EXTSEL_TIM2_CC2 | ADC_CR2_DMA);

	// TIM2 runs at 72 MHz (APB1 x2)
	uint32_t ticks = uint32_t(dt * 72e6f + 0.5f);
	uint32_t psc = ticks / 65536;
	uint32_t arr = ticks / (psc + 1) - 1;
	TIM2->CR1 = 0;
	TIM2->PSC = psc;
	TIM2->ARR = arr;
	TIM2->CCR2 = (arr + 1) / 2;
	TIM2->CCMR1 = 6u << TIM_CCMR1_OC2M_Pos;
	TIM2->CCER = TIM_CCER_CC2E;
	TIM2->EGR = TIM_EGR_UG;
	TIM2->CNT = 0;
	TIM2->CR1 = TIM_CR1_CEN;
}

void startFastMode() {
	ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
	ADC2->SMPR2 &= ~ADC_SMPR2_SMP0;
	ADC1->SQR1 = 0;
	ADC2->SQR1 = 0;
	ADC1->SQR3 = ADC_CH_SIGNAL;
	ADC2->SQR3 = ADC_CH_SIGNAL;
	ADC1->CR1 = (ADC1->CR1 & ~ADC_CR1_DUALMOD) | (7u << ADC_CR1_DUALMOD_Pos);

	DMA1_Channel1->CPAR = uint32_t(&ADC1->DR);
	DMA1_Channel1->CMAR = uint32_t(ring.w);
	DMA1_Channel1->CNDTR = RING / 2;
	DMA1->IFCR = DMA_IFCR_CGIF1;
	DMA1_Channel1->CCR = DMA_CCR_PL_1 | DMA_CCR_MSIZE_1 | DMA_CCR_PSIZE_1 | DMA_CCR_MINC |
	                     DMA_CCR_EN;

	setCr2(ADC2, EXTSEL_SWSTART | ADC_CR2_CONT);
	setCr2(ADC1, EXTSEL_SWSTART | ADC_CR2_CONT | ADC_CR2_DMA);
	ADC1->CR2 |= ADC_CR2_SWSTART;
}

/**
 * Edge detector with hysteresis and holdoff. `idx` is the sample index; a
 * trigger needs the signal to have stayed away from the trigger side for
 * `holdoffSamples` before the edge.
 */
bool crosses(int16_t v, uint32_t idx) {
	bool active = edge == EDGE_RISING ? v >= level : v <= level;
	bool away = edge == EDGE_RISING ? v < level - HYST : v > level + HYST;
	if (away) armed = true;
	if (!active) return false;
	bool fire = armed && idx - lastActive >= holdoffSamples;
	lastActive = idx;
	if (fire) armed = false;
	return fire;
}

void buildFrame(const uint16_t *src, uint32_t mask, uint32_t first, uint16_t trig, bool triggered) {
	for (uint16_t i = 0; i < FRAME_MAX; i++) fr.s[i] = src[(first + i) & mask];
	fr.n = FRAME_MAX;
	fr.trig = trig;
	fr.dt = dt;
	fr.triggered = triggered;
	fr.roll = false;
}

}  // namespace

extern "C" void DMA1_Channel1_IRQHandler() {
	if (DMA1->ISR & DMA_ISR_TCIF1) {
		DMA1->IFCR = DMA_IFCR_CTCIF1;
		wraps++;
	}
}

namespace Acq {

void begin() {
	hal::pinMode(GPIOA, 0, hal::PIN_ANALOG);
	hal::pinMode(GPIOA, 1, hal::PIN_ANALOG);
	hal::pinMode(GPIOA, 2, hal::PIN_ANALOG);
	hal::pinMode(GPIOA, 3, hal::PIN_ANALOG);
	ADC1->CR1 = 0;
	ADC2->CR1 = 0;
	// Slow sampling for the switch dividers on ADC2
	ADC2->SMPR2 = (5u << ADC_SMPR2_SMP1_Pos) | (5u << ADC_SMPR2_SMP2_Pos) |
	              (5u << ADC_SMPR2_SMP3_Pos);
	calibrate(ADC1);
	calibrate(ADC2);
	NVIC_SetPriority(DMA1_Channel1_IRQn, 1);
	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
	fr.n = 0;
}

Switches readSwitches() {
	if (fast && st != ACQ_IDLE) return lastSw;
	auto read = [](uint8_t ch) {
		uint32_t sum = 0;
		ADC2->SQR3 = ch;
		for (uint8_t i = 0; i < 2; i++) {
			ADC2->SR = 0;
			ADC2->CR2 |= ADC_CR2_SWSTART;
			while (!(ADC2->SR & ADC_SR_EOC)) {}
			sum += ADC2->DR;
		}
		return uint16_t(sum / 2);
	};
	uint16_t cpl = read(ADC_CH_CPL);
	uint16_t sen1 = read(ADC_CH_SENS1);
	uint16_t sen2 = read(ADC_CH_SENS2);

	Switches s;
	s.coupling = cpl < 400 ? CPL_GND : cpl < 2000 ? CPL_AC : CPL_DC;
	int8_t base = sen1 < 400 ? RNG_1V : sen1 < 2000 ? RNG_100mV : RNG_10mV;
	int8_t mult = sen2 < 400 ? -2 : sen2 < 2000 ? -1 : 0;
	s.range = Range(base + mult);
	lastSw = s;
	return s;
}

void start(const Settings &s, int16_t lvl, Range r, Coupling c, int16_t zero) {
	haltHw();
	mode = TrigMode(s.trigMode);
	edge = TrigEdge(s.trigEdge);
	level = lvl;
	fast = s.timebase <= TB_FAST_LAST;
	roll = !fast && mode == TRIG_AUTO && s.timebase >= TB_ROLL_FIRST;
	dt = fast ? FAST_DT : TIMEBASE_S[s.timebase] / SAMPLES_PER_DIV;
	holdoffSamples = uint32_t(HOLDOFF_S[s.holdoff < HOLDOFF_COUNT ? s.holdoff : 0] / dt + 0.5f);
	fr.range = r;
	fr.coupling = c;
	fr.zero = zero;
	wraps = 0;
	scanned = 0;
	armed = false;
	forced = false;
	lastActive = 0;
	// Auto fires after 1.5 screens or 80 ms, whichever is longer
	autoTimeoutMs = uint32_t(TIMEBASE_S[s.timebase] * GRID_DIVS_X * 1500.0f);
	if (autoTimeoutMs < 80) autoTimeoutMs = 80;
	armedSince = hal::millis();
	lastRoll = 0;

	if (fast) {
		st = ACQ_ARMED;
		startFastMode();
	} else {
		st = roll ? ACQ_ROLL : ACQ_PREFILL;
		startTimerMode();
	}
}

void stop() {
	haltHw();
	st = ACQ_IDLE;
}

State state() {
	return st;
}

const Frame &frame() {
	return fr;
}

bool poll(uint32_t now) {
	switch (st) {
	case ACQ_IDLE:
		return false;

	case ACQ_ROLL: {
		if (now - lastRoll < ROLL_REFRESH_MS) return false;
		lastRoll = now;
		uint32_t total = written();
		if (total == 0) return false;
		uint16_t n = total < GRID_W ? total : GRID_W;
		uint32_t first = total - n;
		for (uint16_t i = 0; i < n; i++) fr.s[i] = ring.s[(first + i) & (RING - 1)];
		fr.n = n;
		fr.trig = 0;
		fr.dt = dt;
		fr.triggered = false;
		fr.roll = true;
		return true;
	}

	case ACQ_PREFILL:
	case ACQ_ARMED: {
		if (fast) {
			if (!(DMA1->ISR & DMA_ISR_TCIF1)) return false;
			DMA1->IFCR = DMA_IFCR_CGIF1;
			haltHw();
			// In fast interleaved mode ADC2 converts first and ADC1 seven ADC
			// clocks later, but ADC1 sits in the low half of each 32-bit word.
			// Swap the halves so the samples are in time order.
			for (uint16_t i = 0; i < RING / 2; i++) ring.w[i] = (ring.w[i] >> 16) | (ring.w[i] << 16);
			armed = false;
			lastActive = 0;
			int32_t found = -1;
			for (uint16_t i = 1; i <= RING - POST; i++) {
				if (crosses(int16_t(ring.s[i]), i) && i >= FRAME_PRE) { found = i; break; }
			}
			bool timeout = now - armedSince >= autoTimeoutMs;
			if (found < 0 && !(mode == TRIG_AUTO && timeout)) {
				startFastMode();  // try another burst
				return false;
			}
			uint32_t t = found < 0 ? FRAME_PRE : uint32_t(found);
			buildFrame(ring.s, RING - 1, t - FRAME_PRE, FRAME_PRE, found >= 0);
			st = ACQ_IDLE;
			return true;
		}

		uint32_t total = written();
		if (total - scanned > RING - 64) scanned = total - RING / 2;  // fell behind
		if (st == ACQ_PREFILL) {
			if (total < FRAME_PRE) return false;
			st = ACQ_ARMED;
			scanned = total;
			lastActive = total;  // holdoff counts from arming
			armedSince = now;
		}
		while (scanned < total) {
			int16_t v = int16_t(ring.s[scanned & (RING - 1)]);
			if (crosses(v, scanned)) {
				trigAt = scanned;
				st = ACQ_POST;
				break;
			}
			scanned++;
		}
		if (st == ACQ_ARMED && mode == TRIG_AUTO && now - armedSince >= autoTimeoutMs) {
			trigAt = total;
			forced = true;
			st = ACQ_POST;
		}
		return false;
	}

	case ACQ_POST: {
		uint32_t total = written();
		if (total < trigAt + POST) return false;
		TIM2->CR1 = 0;
		haltHw();
		buildFrame(ring.s, RING - 1, trigAt - FRAME_PRE, FRAME_PRE, !forced);
		st = ACQ_IDLE;
		return true;
	}
	}
	return false;
}

int16_t frameMean() {
	if (!fr.n) return 0;
	uint32_t sum = 0;
	for (uint16_t i = 0; i < fr.n; i++) sum += fr.s[i];
	return int16_t(sum / fr.n);
}

}  // namespace Acq
