/****************************************************************************
 * SAMV7 DShot TC+DMA Test — Timer1 (TC0 CH1) on PA15
 *
 * Tests DMA-driven DShot on Timer Counter (NOT PWMC).
 * TC RA register is NOT double-buffered — DMA writes take immediate effect.
 * RC compare event triggers XDMAC to write next RA value from buffer.
 *
 * This is architecturally equivalent to STM32 TIM+DMA:
 *   RC compare → DMA trigger → DMA writes RA → pin duty changes immediately
 *
 * Hardware:
 *   Timer1 = TC0 CH1, base = 0x4000C040
 *   RA register = base + 0x14 = 0x4000C054
 *   XDMAC PERID = 41 (TC1_RX hardware request)
 *   Pin = PA15 (TIOA output)
 *
 * To test: probe PA15 with Saleae after running `dshot start`
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <nuttx/cache.h>
#include <nuttx/irq.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <drivers/drv_dshot.h>
#include <px4_arch/io_timer.h>

#include "arm_internal.h"
#include "hardware/sam_pmc.h"
#include "hardware/sam_tc.h"
#include "sam_gpio.h"

/* ─── TC0 CH1 (Timer1) registers ─── */
#define TC1_CH_BASE           (SAM_TC012_BASE + SAM_TC_CHAN_OFFSET(1))
#define TC1_CCR               (TC1_CH_BASE + SAM_TC_CCR_OFFSET)
#define TC1_CMR               (TC1_CH_BASE + SAM_TC_CMR_OFFSET)
#define TC1_RA                (TC1_CH_BASE + SAM_TC_RA_OFFSET)
#define TC1_RB                (TC1_CH_BASE + SAM_TC_RB_OFFSET)
#define TC1_RC                (TC1_CH_BASE + SAM_TC_RC_OFFSET)
#define TC1_SR                (TC1_CH_BASE + SAM_TC_SR_OFFSET)
#define TC1_IER               (TC1_CH_BASE + SAM_TC_IER_OFFSET)
#define TC1_IDR               (TC1_CH_BASE + SAM_TC_IDR_OFFSET)

/* ─── XDMAC registers ─── */
#define XDMAC_BASE            0x40078000u
#define XDMAC_GE              (XDMAC_BASE + 0x001Cu)
#define XDMAC_GD              (XDMAC_BASE + 0x0020u)
#define XDMAC_GS              (XDMAC_BASE + 0x0024u)

#define DMA_CH                15u  /* Use XDMAC channel 15 (unused) */
#define DMA_CH_BIT            (1u << DMA_CH)
#define DMA_CH_OFF            (XDMAC_BASE + 0x0050u + (DMA_CH * 64u))
#define DMA_CIS               (DMA_CH_OFF + 0x0Cu)
#define DMA_CSA               (DMA_CH_OFF + 0x10u)
#define DMA_CDA               (DMA_CH_OFF + 0x14u)
#define DMA_CNDA              (DMA_CH_OFF + 0x18u)
#define DMA_CNDC              (DMA_CH_OFF + 0x1Cu)
#define DMA_CUBC              (DMA_CH_OFF + 0x20u)
#define DMA_CBC               (DMA_CH_OFF + 0x24u)
#define DMA_CC                (DMA_CH_OFF + 0x28u)
#define DMA_CDSMSP            (DMA_CH_OFF + 0x2Cu)
#define DMA_CSUS              (DMA_CH_OFF + 0x30u)
#define DMA_CDUS              (DMA_CH_OFF + 0x34u)
#define DMA_CID               (DMA_CH_OFF + 0x04u)

/* CC register for TC1 DMA:
 * TYPE=PER, DSYNC=MEM2PER(TX), CSIZE=1, DWIDTH=WORD,
 * SIF=AHB_IF1(memory), DIF=AHB_IF0(peripheral),
 * SAM=INCR, DAM=FIXED, PERID=41 (TC1_RX hardware request)
 */
#define TC_DMA_CC  ((1u << 0)  |  /* TYPE: peripheral */ \
		    (0u << 1)  |  /* MBSIZE: single */ \
		    (1u << 4)  |  /* DSYNC: mem→periph (TX) */ \
		    (0u << 8)  |  /* CSIZE: 1 */ \
		    (2u << 11) |  /* DWIDTH: WORD */ \
		    (1u << 13) |  /* SIF: AHB_IF1 (memory) */ \
		    (0u << 14) |  /* DIF: AHB_IF0 (peripheral) */ \
		    (1u << 16) |  /* SAM: INCR */ \
		    (0u << 18) |  /* DAM: FIXED */ \
		    (41u << 24))  /* PERID: 41 = TC1_RX (Timer1 DMA request) */

/* ─── DShot buffer ─── */
#define DSHOT_PERIODS         17u

/* DShot300 at MCK/8 = 18.75 MHz: RC = 62, T0H = 23, T1H = 46 */
#define TC_RC_DSHOT300        62u
#define TC_T0H                23u
#define TC_T1H                46u

static uint32_t g_tc_buffer[DSHOT_PERIODS] __attribute__((aligned(32)));

/**
 * Initialize TC1 DMA test — call from shell or init
 * Sends a fixed DShot pattern on PA15
 */
void dshot_tc_dma_test(void)
{
	/* Build test buffer: throttle=0 → all T0H, plus reset */
	for (int i = 0; i < 16; i++) {
		g_tc_buffer[i] = TC_T0H;  /* all bits = 0 → short pulse */
	}
	g_tc_buffer[16] = 0;  /* reset period */

	/* Flush cache */
	up_clean_dcache((uintptr_t)g_tc_buffer,
			(uintptr_t)g_tc_buffer + sizeof(g_tc_buffer));

	/* Enable TC0 peripheral clock (PID 23 for TC0 block) */
	putreg32((1u << 23), SAM_PMC_PCER0);  /* TC0 CH0 = PID 23 */
	putreg32((1u << 24), SAM_PMC_PCER0);  /* TC0 CH1 = PID 24 (Timer1) */

	/* Enable XDMAC clock */
	putreg32((1u << 26), SAM_PMC_PCER1);

	/* Configure TC1 for waveform mode */
	putreg32(TC_CCR_CLKDIS, TC1_CCR);  /* disable first */

	uint32_t cmr = TC_CMR_TCCLKS_MCK8 | TC_CMR_WAVE | TC_CMR_WAVSEL_UPRC |
		       TC_CMR_EEVT_XC0 | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET;
	putreg32(cmr, TC1_CMR);

	putreg32(TC_RC_DSHOT300, TC1_RC);  /* Period = 62 ticks = 3.31µs */
	putreg32(TC_T0H, TC1_RA);         /* Initial duty = T0H */

	/* ─── Configure XDMAC channel ─── */
	putreg32(DMA_CH_BIT, XDMAC_GD);  /* disable */
	putreg32(0xFFu, DMA_CID);
	(void)getreg32(DMA_CIS);

	putreg32(TC_DMA_CC, DMA_CC);
	putreg32(TC1_RA, DMA_CDA);        /* Destination = TC1 RA register */
	putreg32((uint32_t)(uintptr_t)g_tc_buffer, DMA_CSA);  /* Source = buffer */
	putreg32(DSHOT_PERIODS, DMA_CUBC); /* 17 transfers */

	/* No linked list, no stride */
	putreg32(0, DMA_CNDC);
	putreg32(0, DMA_CNDA);
	putreg32(0, DMA_CBC);
	putreg32(0, DMA_CDSMSP);
	putreg32(0, DMA_CSUS);
	putreg32(0, DMA_CDUS);

	/* Enable DMA channel — waits for TC1 RC compare trigger */
	putreg32(DMA_CH_BIT, XDMAC_GE);

	/* Configure GPIO PA15 for TIOA1 (Peripheral B) */
	sam_configgpio(timer_io_channels[4].gpio_out);

	/* Start TC1 — begins counting, RC compare fires DMA each period */
	putreg32(TC_CCR_CLKEN | TC_CCR_SWTRG, TC1_CCR);

	PX4_INFO("TC1 DMA test started: PA15, DShot300, RC=%u T0H=%u T1H=%u PERID=41",
		 TC_RC_DSHOT300, TC_T0H, TC_T1H);
	PX4_INFO("  DMA: CC=0x%08" PRIx32 " CDA=0x%08" PRIx32 " CSA=0x%08" PRIx32 " CUBC=%u",
		 (uint32_t)TC_DMA_CC, (uint32_t)TC1_RA, (uint32_t)(uintptr_t)g_tc_buffer, DSHOT_PERIODS);
}
