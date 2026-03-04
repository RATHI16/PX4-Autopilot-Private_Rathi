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
 * @file init.c
 *
 * SAMV71-XULT-specific early startup code.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "board_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <errno.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/mmcsd.h>
#include <nuttx/sdio.h>
#include <nuttx/mm/gran.h>
#include <nuttx/i2c/i2c_master.h>
#include <chip.h>
#include <arch/board/board.h>
#include "arm_internal.h"

#ifdef CONFIG_SAMV7_HSMCI0
#  include "sam_hsmci.h"
#  include "board_hsmci.h"
#endif

#include <px4_arch/io_timer.h>
#include <drivers/drv_hrt.h>

/* PCK6 configuration for 1 MHz HRT clock */
#include "sam_pck.h"
#include "hardware/sam_matrix.h"
#include "hardware/sam_pmc.h"
#include <drivers/drv_board_led.h>
#include <systemlib/px4_macros.h>
#include <px4_platform_common/init.h>
#include <px4_platform/gpio.h>

/* SAMV7 MATRIX CCFG_PCCR register - not in upstream NuttX, define locally */
#ifndef SAM_MATRIX_CCFG_PCCR
#  define SAM_MATRIX_CCFG_PCCR      (SAM_MATRIX_BASE + 0x0118)
#endif
#ifndef MATRIX_CCFG_PCCR_TC0CC
#  define MATRIX_CCFG_PCCR_TC0CC    (1 << 21)  /* TC0 clock selection */
#endif
#include <px4_platform/board_dma_alloc.h>
#include <sys/mount.h>
#include <sys/stat.h>

# if defined(FLASH_BASED_PARAMS)
#  include <parameters/flashparams/flashfs.h>
#endif

/****************************************************************************
 * Pre-Processor Definitions
 ****************************************************************************/

__BEGIN_DECLS
extern void led_init(void);
extern void led_on(int led);
extern void led_off(int led);

/* Nocache region symbols from linker script */
extern uint32_t _s_nocache;
extern uint32_t _e_nocache;

/* Board MPU nocache region init (sam_mpuinit.c) */
extern void board_mpu_nocache_init(void);

/* QSPI flash functions declared in board_config.h */

__END_DECLS

#ifdef CONFIG_SAMV7_HSMCI0
/****************************************************************************
 * Name: samv71_sdcard_initialize
 *
 * Description:
 *   Bring up the SAMV71 HSMCI (slot 0) and mount /fs/microsd.
 *
 ****************************************************************************/
static int samv71_sdcard_initialize(void)
{
	int ret;

	/* Initialize HSMCI — card detect disabled (PD18 used by UART4/RC SBUS) */
	ret = sam_hsmci_initialize(HSMCI0_SLOTNO, HSMCI0_MINOR, 0, 0);

	if (ret < 0) {
		syslog(LOG_ERR, "[sdcard] sam_hsmci_initialize failed: %d\n", ret);
		return ret;
	}

	/* Wait for async card probe to complete (up to 500ms) */
	struct stat buf;
	int timeout_ms = 500;

	while (stat("/dev/mmcsd0", &buf) < 0 && timeout_ms > 0) {
		up_mdelay(50);
		timeout_ms -= 50;
	}

	/* Do NOT mount here — let rcS handle mounting so STORAGE_AVAILABLE is set correctly */
	return ret;
}
#endif /* CONFIG_SAMV7_HSMCI0 */

/************************************************************************************
 * Name: board_peripheral_reset
 *
 * Description:
 *
 ************************************************************************************/
__EXPORT void board_peripheral_reset(int ms)
{
	UNUSED(ms);
}

/************************************************************************************
 * Name: board_on_reset
 *
 * Description:
 * Optionally provided function called on entry to board_system_reset
 * It should perform any house keeping prior to the rest.
 *
 * status - 1 if resetting to boot loader
 *          0 if just resetting
 *
 ************************************************************************************/
__EXPORT void board_on_reset(int status)
{
	/*
	 * Safety: disable all PWMC outputs and drive motor pins low.
	 * Uses self-contained register constants so this works even if
	 * the PWMC driver has not been initialized.
	 */

#define SAMV7_PWM0_BASE     0x40020000
#define SAMV7_PWM_DIS       0x008       /* PWM Disable Register offset */
#define SAMV7_PWM_DIS_ALL   0x0F        /* Channels 0-3 disable mask */

	/* 1. Disable all PWM0 channels via hardware register */
	putreg32(SAMV7_PWM_DIS_ALL, SAMV7_PWM0_BASE + SAMV7_PWM_DIS);

	/* 2. Reconfigure each motor GPIO from peripheral-mux to output-low.
	 *    Preserve port+pin bits, replace mode with GPIO_OUTPUT.
	 */
	for (int i = 0; i < DIRECT_PWM_OUTPUT_CHANNELS; i++) {
		uint32_t pin_id = timer_io_channels[i].gpio_out & (GPIO_PORT_MASK | GPIO_PIN_MASK);
		sam_configgpio(GPIO_OUTPUT | GPIO_CFG_DEFAULT | GPIO_OUTPUT_CLEAR | pin_id);
	}

#undef SAMV7_PWM0_BASE
#undef SAMV7_PWM_DIS
#undef SAMV7_PWM_DIS_ALL

	UNUSED(status);
}

/************************************************************************************
 * HRT PCK6 configuration (shared with hrt.c via weak symbol)
 ************************************************************************************/
volatile bool g_samv7_hrt_pck6_configured = false;

static void configure_hrt_pck6(void)
{
	uint32_t actual_freq;
	uint32_t regval;
	int timeout = 100000;

	g_samv7_hrt_pck6_configured = false;

	actual_freq = sam_pck_configure(PCK6, PCKSRC_MCK, 1000000);

	if (actual_freq != 1000000) {
		syslog(LOG_ERR, "[hrt] PCK6 configure failed (%lu Hz)\n",
		       (unsigned long)actual_freq);
		return;
	}

	sam_pck_enable(PCK6, true);

	while ((getreg32(SAM_PMC_SR) & PMC_INT_PCKRDY6) == 0) {
		if (--timeout <= 0) {
			syslog(LOG_ERR, "[hrt] PCK6 ready timeout\n");
			sam_pck_enable(PCK6, false);
			return;
		}
	}

	/* Disable MATRIX write protection (if previously enabled) */
	putreg32(MATRIX_WPMR_WPKEY, SAM_MATRIX_WPMR);

	/* Route TC0 clock to PCK6 (TC0CC = 0) */
	regval = getreg32(SAM_MATRIX_CCFG_PCCR);
	regval &= ~MATRIX_CCFG_PCCR_TC0CC;
	putreg32(regval, SAM_MATRIX_CCFG_PCCR);

	g_samv7_hrt_pck6_configured = true;
	syslog(LOG_INFO, "[hrt] PCK6 configured for 1 MHz TC0 clock\n");
}

/************************************************************************************
 * Name: sam_boardinitialize
 *
 * Description:
 *   All SAM V71 architectures must provide the following entry point.  This entry
 *   point is called early in the initialization -- after all memory has been
 *   configured and mapped but before any devices have been initialized.
 *
 ************************************************************************************/

__EXPORT void
sam_boardinitialize(void)
{
	board_on_reset(-1); /* Reset PWM first thing */

	/* Enable DWT cycle counter for perf/critmon — must be early,
	 * before any code that uses up_perf_gettime() (SCHED_CRITMONITOR).
	 * BOARD_CPU_FREQUENCY (300 MHz) is the DWT CYCCNT clock source.
	 */
	up_perf_init((void *)(uintptr_t)BOARD_CPU_FREQUENCY);

	/* Zero out the nocache region (as it is NOLOAD) */
	uint32_t *dest;
	for (dest = &_s_nocache; dest < &_e_nocache; ) {
		*dest++ = 0;
	}

	/* Configure MPU nocache region for DMA buffers - MUST be done here,
	 * before sam_mpu_initialize() enables the MPU and before D-cache
	 * is enabled. This ensures DMA buffers are properly marked as
	 * non-cacheable from the start.
	 */
	board_mpu_nocache_init();

	/* Configure HRT clock before any timer users come up */
	configure_hrt_pck6();

	/* configure LEDs */
	board_autoled_initialize();
	led_init();

	/* configure pins */
	const uint32_t gpio[] = PX4_GPIO_INIT_LIST;
	px4_gpio_init(gpio, arraySize(gpio));

	/* configure USB interfaces */
	sam_usbinitialize();

	/* I2C initialization moved to board_app_initialize to avoid early interrupt issues */
}

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application specific initialization.  This function is never
 *   called directly from application code, but only indirectly via the
 *   (non-standard) boardctl() interface using the command BOARDIOC_INIT.
 *
 ****************************************************************************/

__EXPORT int board_app_initialize(uintptr_t arg)
{
	px4_platform_init();

	/* Initialize DMA allocator BEFORE SD card — async probe uses DMA from nocache */
	if (board_dma_alloc_init() < 0) {
		syslog(LOG_ERR, "[boot] DMA alloc init failed\n");
	}

#ifdef CONFIG_SAMV7_QSPI_SPI_MODE
	int qspi_ret = board_qspi_flash_init();

	if (qspi_ret < 0) {
		syslog(LOG_ERR, "[boot] QSPI flash init failed: %d\n", qspi_ret);

	} else {
		struct mtd_dev_s *qspi_mtd = board_get_qspi_mtd();

		if (qspi_mtd) {
			int part_ret = board_qspi_create_partitions(qspi_mtd);

			if (part_ret < 0) {
				syslog(LOG_ERR, "[boot] QSPI partition setup failed: %d\n", part_ret);
			}
		}
	}

#endif

#ifdef CONFIG_SAMV7_HSMCI0
	if (samv71_sdcard_initialize() < 0) {
		syslog(LOG_ERR, "[boot] SD initialization failed\n");
	}
#endif

	/* Initialize I2C buses - must be after px4_platform_init */
#ifdef CONFIG_SAMV7_TWIHS0
	struct i2c_master_s *i2c0 = sam_i2cbus_initialize(0);

	if (i2c0 == NULL) {
		syslog(LOG_ERR, "[boot] Failed to initialize I2C bus 0\n");

	} else {
		int ret = i2c_register(i2c0, 0);

		if (ret < 0) {
			syslog(LOG_ERR, "[boot] Failed to register I2C bus 0: %d\n", ret);
		}
	}
#endif

	drv_led_start();

	led_off(LED_RED);
	led_on(LED_GREEN); // Indicate Power
	led_off(LED_BLUE);

	/* HAS_PROGMEM disabled — progmem_dump_initialize() hangs on SAMV7.
	 * board_hardfault_init() is a no-op without HAS_PROGMEM or HAS_BBSRAM,
	 * but skip the call entirely to avoid confusion.
	 */
#ifdef HAS_PROGMEM
	if (board_hardfault_init(2, true) != 0) {
		led_on(LED_RED);
		syslog(LOG_ERR, "[boot] Hardfault init FAILED\n");
	}
#endif

	syslog(LOG_INFO, "[boot] Parameters on /fs/mtd_params (QSPI), backup on SD\n");

	syslog(LOG_INFO, "[boot] Board initialization complete\n");

	return OK;
}
