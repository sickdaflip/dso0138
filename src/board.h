// SPDX-License-Identifier: MIT
/**
 * DSO138 (JYE Tech 13803K/13804K, firmware 113-13801-05x/06x hardware)
 * pin map and hardware constants. STM32F103C8, 8 MHz crystal.
 */
#pragma once
#include <stdint.h>
#ifndef DSO_SIM
#include "stm32f1xx.h"
#endif

#define FW_NAME    "DSO138 Open"
#define FW_VERSION "1.3.0"

// ---------------------------------------------------------------------------
// TFT (ILI9341, 8-bit parallel 8080 bus)
// Data D0..D7 = PB0..PB7 (PB3/PB4 need JTAG disabled)
// ---------------------------------------------------------------------------
#define TFT_RD_PORT  GPIOB
#define TFT_RD_PIN   10
#define TFT_RST_PORT GPIOB
#define TFT_RST_PIN  11
#define TFT_CS_PORT  GPIOC
#define TFT_CS_PIN   13
#define TFT_RS_PORT  GPIOC
#define TFT_RS_PIN   14
#define TFT_WR_PORT  GPIOC
#define TFT_WR_PIN   15

// ---------------------------------------------------------------------------
// Analog front end
// ---------------------------------------------------------------------------
#define ADC_CH_SIGNAL   0   // PA0: channel 1 after the op-amp stage
#define ADC_CH_SENS2    1   // PA1: SEN2 switch (x1/x2/x5) divider
#define ADC_CH_SENS1    2   // PA2: SEN1 switch (1V/0.1V/10mV) divider
#define ADC_CH_CPL      3   // PA3: coupling switch (GND/AC/DC) divider

// ---------------------------------------------------------------------------
// Timers / PWM
//   TIM2 CC2  -> ADC sample clock (internal event only)
//   TIM3 CH2  -> PA7  1 kHz / 3.3 V test signal
//   TIM4 CH3  -> PB8  trigger reference (50 %)
//   TIM4 CH4  -> PB9  charge pump for the negative supply (20 kHz)
// ---------------------------------------------------------------------------
#define VGEN_FREQ_HZ      20000
#define VGEN_DUTY_PERMILLE 194   // measured on the stock firmware: ~19.4 %

// ---------------------------------------------------------------------------
// Buttons (active low, internal pull-ups)
// ---------------------------------------------------------------------------
#define BTN_SEL_PIN   12   // PB12
#define BTN_MINUS_PIN 13   // PB13
#define BTN_PLUS_PIN  14   // PB14
#define BTN_OK_PIN    15   // PB15

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
#define LED_PORT GPIOA
#define LED_PIN  15        // PA15 (needs JTAG disabled)

#define SERIAL_BAUD 115200 // USART1, PA9 TX / PA10 RX on header J5

// ---------------------------------------------------------------------------
// Display geometry (landscape)
// ---------------------------------------------------------------------------
#define LCD_W 320
#define LCD_H 240
