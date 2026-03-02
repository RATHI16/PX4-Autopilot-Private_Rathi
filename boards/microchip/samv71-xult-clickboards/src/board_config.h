/****************************************************************************
 *
 *   Copyright (c) 2024 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file board_config.h
 *
 * SAMV71-XULT with Click sensor boards internal definitions
 */

#pragma once

/****************************************************************************************************
 * Included Files
 ****************************************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/compiler.h>
#include <stdint.h>

#include <sam_gpio.h>

/****************************************************************************************************
 * Definitions
 ****************************************************************************************************/

/* GPIOs ***********************************************************************************/

/* LEDs - SAMV71-XULT has two LEDs:
 *   PA23 - Yellow LED0
 *   PC9  - Yellow LED1
 */

#define GPIO_nLED_BLUE       /* PA23 */  (GPIO_OUTPUT|GPIO_CFG_PULLUP|GPIO_OUTPUT_SET|GPIO_PORT_PIOA|GPIO_PIN23)

/* Only one LED available on SAMV71-XULT - PA23 (Blue LED)
 * Driver will use drv_board_led.h defaults:
 * LED_BLUE=0, LED_AMBER=1, LED_RED=1, LED_GREEN=3
 * Board only implements LED_BLUE (index 0)
 */

#define BOARD_HAS_CONTROL_STATUS_LEDS      1
#define BOARD_ARMED_STATE_LED  LED_BLUE

/* ICM45686 on EXT2 header (replaces ICM20689 — PD25 freed for GPS UART2)
 * EXT2 Pin 15 = CS  = PD27
 * EXT2 Pin 9  = IRQ = PD28 (DRDY)
 */
#define GPIO_SPI0_CS_ICM45686    (GPIO_OUTPUT|GPIO_OUTPUT_SET|GPIO_PORT_PIOD|GPIO_PIN27)
#define GPIO_SPI0_DRDY_ICM45686  (GPIO_INPUT|GPIO_CFG_PULLUP|GPIO_INT_FALLING|GPIO_PORT_PIOD|GPIO_PIN28)
#define GPIO_SPI0_DRDY_ICM45686_IRQ  SAM_IRQ_PD28

/* BMP388 — SPI CS removed (PD27 now used by ICM45686). BMP388 runs via I2C. */

/* mikroBUS Socket RST pins - Active LOW, start HIGH to release reset
 * Socket 1: PA19 (RST), PA0 (INT)
 * Socket 2: PB0 (RST) - NOT AVAILABLE: PB0 used for PWMC Motor 4
 * EXT1 adapter: PA5 (RST) - for Xplained Pro extension reset line (EXT1 pin 10)
 * EXT2 adapter: PA24 (RST) - for Xplained Pro extension reset line (EXT2 pin 10)
 * Compass 4 Click (AK09915) requires RST pin HIGH to operate
 *
 * NOTE: GPIO_MB2_RST (PB0) removed - pin now used for PWMC0 CH0 (Motor 4)
 */
#define GPIO_MB1_RST     (GPIO_OUTPUT|GPIO_OUTPUT_SET|GPIO_PORT_PIOA|GPIO_PIN19)
/* GPIO_MB2_RST (PB0) REMOVED - used for PWMC Motor 4 */
#define GPIO_EXT1_RST    (GPIO_OUTPUT|GPIO_OUTPUT_SET|GPIO_PORT_PIOA|GPIO_PIN5)
#define GPIO_EXT2_RST    (GPIO_OUTPUT|GPIO_OUTPUT_SET|GPIO_PORT_PIOA|GPIO_PIN24)

/* Primary storage defaults to SD card. Enable BOARD_HAS_FRAM_CLICK (and re-add
 * FLASH_BASED_PARAMS) only when a FRAM Click board or other flash backend is
 * present.
 */
// #define FLASH_BASED_PARAMS

/* ADC Configuration ***********************************************************************************/

/* SAMV7 AFEC0 Base Address for PX4 ADC driver */
#define BOARD_ADC_BASE          SAM_AFEC0_BASE  /* 0x4003c000 */

/* ADC Reference Voltage (VDDANA = 3.3V on SAMV71-XULT) */
#define BOARD_ADC_POS_REF_V     3.3f

/* Battery monitoring channels on AFEC0:
 * - Channel 0 (PD30): Battery Voltage
 * - Channel 7 (PA18): Battery Current
 */
#define ADC_BATTERY_VOLTAGE_CHANNEL  0    /* PD30 - AFEC0_AD0 */
#define ADC_BATTERY_CURRENT_CHANNEL  7    /* PA18 - AFEC0_AD7 */

/* ADC channels bitmask (for driver initialization)
 * NOTE: If ADC_CHANNELS is expanded, corresponding GPIO_AFE0_ADx pins must be
 * configured in platforms/nuttx/src/px4/microchip/samv7/adc/adc.cpp
 */
#define ADC_CHANNELS ((1 << ADC_BATTERY_VOLTAGE_CHANNEL) | (1 << ADC_BATTERY_CURRENT_CHANNEL))

/* Battery brick configuration
 * BOARD_NUMBER_BRICKS: Number of power input bricks (1 for this dev board)
 * BOARD_ADC_BRICK_VALID: ADC channel for brick validity detection, or constant 1
 *   to report always-valid (no HW detection). board_common.h uses this to
 *   generate BOARD_BRICK_VALID_LIST for single-brick boards.
 */
#define BOARD_NUMBER_BRICKS          1
#define BOARD_ADC_BRICK_VALID        1  /* Always valid - no HW detection on dev board */

/* Safety Button and LED Configuration *************************************************************/

/* Safety Button: SW0 (PA9) - ACTIVE LOW (pressed = GND)
 * SafetyButton driver expects active-HIGH, so we define BOARD_SAFETY_BUTTON_ACTIVE_LOW
 * to signal that the driver should invert the read.
 */
#define GPIO_BTN_SAFETY       (GPIO_INPUT | GPIO_CFG_PULLUP | GPIO_PORT_PIOA | GPIO_PIN9)
#define BOARD_SAFETY_BUTTON_ACTIVE_LOW  1  /* Required: driver must invert read */

/* Safety LED: LED1 (PC9) - Active LOW (LED on when pin LOW) */
#define GPIO_LED_SAFETY       (GPIO_OUTPUT | GPIO_CFG_DEFAULT | GPIO_OUTPUT_SET | GPIO_PORT_PIOC | GPIO_PIN9)

/* Armed Status Output: PA20 - nARMED signal for external indication
 * Active LOW: LOW = armed, HIGH = not armed
 * _INIT version for initialization list, GPIO_nARMED for runtime
 */
#define GPIO_nARMED_INIT      (GPIO_OUTPUT | GPIO_CFG_PULLUP | GPIO_OUTPUT_SET | GPIO_PORT_PIOA | GPIO_PIN20)
#define GPIO_nARMED           (GPIO_OUTPUT | GPIO_CFG_DEFAULT | GPIO_OUTPUT_CLEAR | GPIO_PORT_PIOA | GPIO_PIN20)

/* External lockout state macros (active low: LOW=armed, HIGH=not armed) */
#define BOARD_INDICATE_EXTERNAL_LOCKOUT_STATE(enabled) \
	px4_arch_gpiowrite(GPIO_nARMED, !(enabled))
#define BOARD_GET_EXTERNAL_LOCKOUT_STATE() \
	(!px4_arch_gpioread(GPIO_nARMED))

/* I2C Buses ***********************************************************************************/

/* SAMV71-XULT I2C Configuration:
 * I2C0 (TWIHS0): All sensors on mikroBUS sockets and Arduino headers
 *   PA3 - TWD0 (SDA)
 *   PA4 - TWCK0 (SCL)
 */

#define PX4_NUMBER_I2C_BUSES 1
#define BOARD_NUMBER_I2C_BUSES 1

/* PWM Configuration ***********************************************************************************/

/* SAMV71-XULT PWM Configuration using PWMC (PWM Controller):
 * PWM0 Module provides 4 independent channels for motor control.
 *
 * Motor Pin Mapping:
 *   Motor 1 (CH3): PC13 - GPIO_PWMC0_H3 (Peripheral B) - EXT2 Pin 4
 *   Motor 2 (CH1): PA2  - GPIO_PWMC0_H1 (Peripheral A) - EXT2 Pin 9
 *   Motor 3 (CH2): PC19 - GPIO_PWMC0_H2 (Peripheral B) - EXT2 Pin 7
 *   Motor 4 (CH0): PB0  - GPIO_PWMC0_H0 (Peripheral A) - EXT1 Pin 13
 *
 * CRITICAL: PA7 was originally used for Motor 1 but conflicts with XIN32
 *           (32.768 kHz slow crystal) when BOARD_HAVE_SLOWXTAL=1. Moved to PC13.
 *
 * Clock Configuration:
 *   MCK = 150MHz, CPRE = 3 (MCK/8 = 18.75MHz)
 *   For 400Hz PWM: CPRD = 46875
 *   LIMITATION: Minimum frequency ~286 Hz (16-bit CPRD overflow at lower rates)
 *
 * NOTE: PB0 (Motor 4) was previously GPIO_MB2_RST. Also conflicts with UART0_TXD.
 *       PA9 (Safety Button) conflicts with UART0_RXD when UART0 is enabled.
 *
 * Timer/Counter usage (separate from PWMC):
 *   TC0 CH0 (TC0) - Reserved for HRT (high-resolution timer)
 *   TC1 CH2 (TC5) - Reserved for RC Input capture: PC29 (TIOA5)
 */

#define DIRECT_PWM_OUTPUT_CHANNELS  4

/* RC Input capture - TC5 (TC1 CH2) - Reserved for future use */
#define GPIO_RC_INPUT    (GPIO_PERIPHB | GPIO_CFG_DEFAULT | GPIO_PORT_PIOC | GPIO_PIN29)  /* TC5 TIOA - PC29 */

/* High-resolution timer */
#define HRT_TIMER               0  /* use TC0 channel 0 for the HRT */
#define HRT_TIMER_CHANNEL       0  /* use capture/compare channel 0 */

/* QSPI Flash — S25FL116K 2MB on SAMV71-XULT ***********************************************/

#ifdef CONFIG_SAMV7_QSPI_SPI_MODE
#define BOARD_HAS_QSPI_FLASH   1

/* QSPI Flash Partition Layout (in erase sectors; S25FL116K uses 4 KB sectors)
 *
 * | Partition | Path             | Sectors | Size   | Purpose                      |
 * |-----------|------------------|---------|--------|------------------------------|
 * | 0         | /fs/mtd_params   | 32      | 128 KB | System parameters (BSON)     |
 * | 1         | /fs/mtd_caldata  | 16      |  64 KB | Factory calibration backup   |
 * | 2         | /fs/mtd_waypoints| 128     | 512 KB | Dataman (missions, etc.)     |
 * | Reserved  | —                | 336     | 1344KB | Future use                   |
 * | Total     |                  | 512     | 2048KB |                              |
 *
 * mtd_partition() takes MTD blocks (geo.blocksize), not erase sectors.
 * Conversion: mtd_blocks = erase_sectors * (geo.erasesize / geo.blocksize)
 */
#define QSPI_PART_PARAMS_OFFSET     0     /* erase sector offset */
#define QSPI_PART_PARAMS_SECTORS    32    /* 128 KB */
#define QSPI_PART_CALDATA_OFFSET    32    /* erase sector offset */
#define QSPI_PART_CALDATA_SECTORS   16    /*  64 KB */
#define QSPI_PART_WAYPOINTS_OFFSET  48    /* erase sector offset */
#define QSPI_PART_WAYPOINTS_SECTORS 128   /* 512 KB */

#define QSPI_NUM_PARTITIONS         3

#endif /* CONFIG_SAMV7_QSPI_SPI_MODE */

/* HSMCI SD Card ***************************************************************************/

#ifdef CONFIG_SAMV7_HSMCI0
#  define HSMCI0_SLOTNO      0
#  define HSMCI0_MINOR       0
  /* Card Detect DISABLED: PD18 conflicts with UART4 (RC SBUS input).
   * Pass cdcfg=0, cdirq=0 to sam_hsmci_initialize() — card always present.
   */
#endif

/* USB ***********************************************************************************/

/* SAMV71 has USB high-speed device */

/* This board provides a DMA pool */
#define BOARD_DMA_ALLOC_POOL_SIZE 5120

/* This board provides the board_on_reset interface */
#define BOARD_HAS_ON_RESET 1

/* Hardfault crash dump — PROGMEM (internal flash reserved sectors)
 * SAMV7 internal flash: 2MB at 0x00400000, sectors are 128KB each.
 * CONFIG_SAMV7_PROGMEM_NSECTORS=2 reserves the last 256KB (0x005C0000-0x005FFFFF)
 * for crash dumps via the NuttX progmem driver.
 */
#define PROGMEM_DUMP_BASE         0x005C0000u  /* Start of reserved progmem region */
#define PROGMEM_DUMP_SIZE         (256 * 1024) /* 2 sectors x 128KB = 256KB */
#define PROGMEM_DUMP_ALIGNMENT    128          /* Header alignment (bytes) */
#define PROGMEM_DUMP_HEADER_PAD   108          /* Padding: ALIGNMENT - sizeof(fixed fields) = 128 - 20 */
#define PROGMEM_DUMP_ERASE_VALUE  0xFF         /* Erased flash byte value */
#define PROGMEM_DUMP_STACK_SIZE   6656         /* Max bytes for user+interrupt stack capture */

/* Reboot counter — the PROGMEM file scheme (files 0-3) has no dedicated
 * reboot counter slot (unlike BBSRAM which has file 0 for that purpose).
 * Store on SD card. If SD isn't mounted during early boot, the open fails
 * gracefully and reboot-loop detection is skipped (crash dump still works).
 */
#define HARDFAULT_REBOOT_PATH     "/fs/microsd/.hardfault_reboot_count"

#define PX4_GPIO_INIT_LIST { \
		GPIO_nLED_BLUE,           \
		GPIO_SPI0_CS_ICM45686,    \
		GPIO_SPI0_DRDY_ICM45686,  \
		GPIO_MB1_RST,             \
		GPIO_EXT1_RST,            \
		GPIO_EXT2_RST,            \
		GPIO_BTN_SAFETY,          \
		GPIO_LED_SAFETY,          \
		GPIO_nARMED_INIT,         \
	}

// Console buffer - ENABLED: lazy initialization implemented in console_buffer.cpp
// (ensure_initialized() with double-checked locking avoids static init issues)
#define BOARD_ENABLE_CONSOLE_BUFFER

/* Number of IO timers used for PWM (PWMC modules)
 * Using PWM0 only - provides 4 channels (CH0-CH3)
 */
#define BOARD_NUM_IO_TIMERS 1

__BEGIN_DECLS

/****************************************************************************************************
 * Public Types
 ****************************************************************************************************/

/****************************************************************************************************
 * Public data
 ****************************************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************************************
 * Public Functions
 ****************************************************************************************************/

extern void sam_usbinitialize(void);

extern void board_peripheral_reset(int ms);

#ifdef CONFIG_SAMV7_QSPI_SPI_MODE
#include <nuttx/mtd/mtd.h>
extern int board_qspi_flash_init(void);
extern struct mtd_dev_s *board_get_qspi_mtd(void);
extern int board_qspi_create_partitions(struct mtd_dev_s *mtd);
#endif

#include <px4_platform_common/board_common.h>

#endif /* __ASSEMBLY__ */

__END_DECLS
