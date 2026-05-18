/****************************************************************************
 * boards/microchip/samd21-io/src/board_config.h
 *
 * PX4IO co-processor board configuration for SAMD21J18A (64-pin).
 *
 * Pin assignments (adjust to match your PCB layout):
 *
 *  FMU UART  : SERCOM5  PB16(TX) / PB17(RX)  @ 1.5 Mbps
 *  SBUS in   : SERCOM3  PA25(RX)              @ 100 kbps inverted
 *  DSM in    : SERCOM0  PA5(RX)               @ 115200
 *  PWM 1-2   : TCC2     PA0 / PA1
 *  PWM 3-4   : TCC1     PA6 / PA7
 *  PWM 5-8   : TCC0     PA8 / PA9 / PA10 / PA19
 *  Safety SW : PB8 (input, pull-up)
 *  LEDs      : PB0(Blue) PB1(Amber) PB2(Safety) PB3(Green)
 *  VSERVO    : PA2  AIN[0]  (3:1 divider)
 *  RSSI ADC  : PA3  AIN[1]  (1:1)
 *  SBUS OE   : PA28 (active low output enable)
 *  Servo flt : PB9  (input, pull-up, active low)
 *  Spektrum  : PB10 (power enable, active high)
 *              PA5  (bind signal, open-drain via SERCOM0 RX GPIO)
 ****************************************************************************/

#pragma once

/* PX4IO has no I2C or SPI buses; these are required by board_common.h */
#define PX4_NUMBER_I2C_BUSES  1
#define PX4_NUMBER_SPI_BUSES  0

#include <px4_platform_common/px4_config.h>
#include <nuttx/compiler.h>
#include <stdint.h>
#include <arch/board/board.h>
#include <chip.h>
#include <sam_port.h>
#include <sam_pinmap.h>

/*--------------------------------------------------------------------------
 * FMU serial link (SERCOM5)
 *--------------------------------------------------------------------------*/
#define PX4FMU_SERCOM_BASE          SAM_SERCOM5_BASE
#define PX4FMU_SERCOM_IRQ           SAM_IRQ_SERCOM5
#define PX4FMU_SERCOM_GCLK_ID_CORE  SAMD21_GCLK_SERCOM5_CORE
#define PX4FMU_SERCOM_GCLK_GEN      0              /* GCLK0 = 48 MHz */
#define PX4FMU_SERIAL_BITRATE       1500000

/* DMAC trigger IDs (from samd_dmac.h) */
#define PX4FMU_SERIAL_TX_DMAC_TRIGSRC  DMAC_TRIGSRC_SERCOM5_TX  /* 12 */
#define PX4FMU_SERIAL_RX_DMAC_TRIGSRC  DMAC_TRIGSRC_SERCOM5_RX  /* 11 */

/* GPIO pin mux for SERCOM5 pads */
#define GPIO_FMU_TX  (PORT_SERCOM5_PAD0_1)   /* PB16 FUNCC */
#define GPIO_FMU_RX  (PORT_SERCOM5_PAD1_1)   /* PB17 FUNCC */

/*--------------------------------------------------------------------------
 * LEDs  (active high)
 *--------------------------------------------------------------------------*/
#define GPIO_LED_BLUE    (PORT_OUTPUT | PORT_PULL_NONE | PORTB | PORT_PIN0)
#define GPIO_LED_AMBER   (PORT_OUTPUT | PORT_PULL_NONE | PORTB | PORT_PIN1)
#define GPIO_LED_SAFETY  (PORT_OUTPUT | PORT_PULL_NONE | PORTB | PORT_PIN2)
#define GPIO_LED_GREEN   (PORT_OUTPUT | PORT_PULL_NONE | PORTB | PORT_PIN3)

#define LED_BLUE(on)    sam_portwrite(GPIO_LED_BLUE,   (on))
#define LED_AMBER(on)   sam_portwrite(GPIO_LED_AMBER,  (on))
#define LED_SAFETY(on)  sam_portwrite(GPIO_LED_SAFETY, (on))
#define LED_GREEN(on)   sam_portwrite(GPIO_LED_GREEN,  (on))

/*--------------------------------------------------------------------------
 * Safety switch (PB8, active low with internal pull-up)
 *--------------------------------------------------------------------------*/
#define GPIO_BTN_SAFETY  (PORT_INPUT | PORT_PULL_UP | PORTB | PORT_PIN8)

/*--------------------------------------------------------------------------
 * Servo fault detect (PB9, active low with internal pull-up)
 *--------------------------------------------------------------------------*/
#define GPIO_SERVO_FAULT_DETECT  (PORT_INPUT | PORT_PULL_UP | PORTB | PORT_PIN9)
/* VDD_SERVO_FAULT defined in px4io.h using px4_arch_gpioread(GPIO_SERVO_FAULT_DETECT) */

/*--------------------------------------------------------------------------
 * SBUS output enable (PA28, active low)
 *--------------------------------------------------------------------------*/
#define GPIO_SBUS_OENABLE  (PORT_OUTPUT | PORT_PULL_NONE | PORTA | PORT_PIN28)
/* ENABLE_SBUS_OUT defined in px4io.h using px4_arch_gpiowrite(GPIO_SBUS_OENABLE,...) */

/*--------------------------------------------------------------------------
 * Spektrum power + bind
 *--------------------------------------------------------------------------*/
#define GPIO_SPEKTRUM_PWR_EN  (PORT_OUTPUT | PORT_PULL_NONE | PORTB | PORT_PIN10)
#define SPEKTRUM_POWER(_on)   sam_portwrite(GPIO_SPEKTRUM_PWR_EN, (_on))

/* PA5 doubles as SERCOM0 RX (DSM) and bind signal output */
#define GPIO_SPEKTRUM_OUT  (PORT_OUTPUT | PORTA | PORT_PIN5)
#define SPEKTRUM_OUT(_v)   sam_portwrite(GPIO_SPEKTRUM_OUT, (_v))
#define SPEKTRUM_RX_AS_UART()         sam_configport(PORT_SERCOM0_PAD1_2)
#define SPEKTRUM_RX_AS_GPIO_OUTPUT()  sam_configport(GPIO_SPEKTRUM_OUT)

/*--------------------------------------------------------------------------
 * ADC inputs
 * ADC_VSERVO, ADC_RSSI, PX4IO_ADC_CHANNEL_COUNT defined in px4io.h
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 * PWM outputs
 * TCC2: WO[0]=PA0, WO[1]=PA1           → channels 1-2
 * TCC1: WO[0]=PA6, WO[1]=PA7           → channels 3-4
 * TCC0: WO[0]=PA8, WO[1]=PA9,
 *        WO[2]=PA10, WO[3]=PA19 (FUNCF) → channels 5-8
 *--------------------------------------------------------------------------*/
#define GPIO_PWM1  (PORT_FUNCE | PORTA | PORT_PIN0)   /* TCC2_WO0 */
#define GPIO_PWM2  (PORT_FUNCE | PORTA | PORT_PIN1)   /* TCC2_WO1 */
#define GPIO_PWM3  (PORT_FUNCE | PORTA | PORT_PIN6)   /* TCC1_WO0 */
#define GPIO_PWM4  (PORT_FUNCE | PORTA | PORT_PIN7)   /* TCC1_WO1 */
#define GPIO_PWM5  (PORT_FUNCE | PORTA | PORT_PIN8)   /* TCC0_WO0 */
#define GPIO_PWM6  (PORT_FUNCE | PORTA | PORT_PIN9)   /* TCC0_WO1 */
#define GPIO_PWM7  (PORT_FUNCE | PORTA | PORT_PIN10)  /* TCC0_WO2 */
#define GPIO_PWM8  (PORT_FUNCF | PORTA | PORT_PIN19)  /* TCC0_WO3 */

#define DIRECT_PWM_OUTPUT_CHANNELS  8
#define BOARD_HAS_NO_CAPTURE

/*--------------------------------------------------------------------------
 * HRT (high-resolution timer) — use TC3
 *--------------------------------------------------------------------------*/
#define HRT_TIMER          3   /* TC3 */
#define HRT_TIMER_CHANNEL  0

/*--------------------------------------------------------------------------
 * Number of IO timer groups (TCC0, TCC1, TCC2)
 *--------------------------------------------------------------------------*/
#define BOARD_NUM_IO_TIMERS  3

#include <px4_platform_common/board_common.h>
