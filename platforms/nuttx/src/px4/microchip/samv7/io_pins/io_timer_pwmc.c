/****************************************************************************
 *
 *   Copyright (c) 2024-2025 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the distribution.
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
 * @file io_timer_pwmc.c
 *
 * SAMV7 IO Timer implementation using PWMC (PWM Controller) for motor PWM.
 *
 * PWMC provides 4 independent channels per module (PWM0, PWM1).
 * Each channel has:
 * - CPRD: Period register (16-bit, determines PWM frequency)
 * - CDTY: Duty cycle register
 * - CDTYUPD: Duty cycle update register (glitch-free updates)
 *
 * Clock configuration (dynamic prescaler selection):
 * - MCK = 150MHz
 * - For rates >= 286 Hz: CPRE = 3 (MCK/8 = 18.75MHz)
 * - For rates < 286 Hz:  CPRE = 6 (MCK/64 = 2.34MHz)
 *
 * CPRD register is 16-bit (max 65535), so prescaler must be selected
 * based on the target rate to prevent overflow:
 *   400 Hz @ MCK/8:  period = 46875  (fits)
 *   50 Hz  @ MCK/64: period = 46875  (fits)
 *   50 Hz  @ MCK/8:  period = 375000 (OVERFLOW - won't work!)
 *
 * Pin mapping:
 * - Motor 1 (CH3): PC13 - GPIO_PWMC0_H3 (Peripheral B) - was PA7, moved due to XIN32 conflict
 * - Motor 2 (CH1): PA2  - GPIO_PWMC0_H1 (Peripheral A)
 * - Motor 3 (CH2): PC19 - GPIO_PWMC0_H2 (Peripheral B)
 * - Motor 4 (CH0): PB0  - GPIO_PWMC0_H0 (Peripheral A)
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <drivers/drv_pwm_output.h>

#include <px4_arch/io_timer.h>
#include <board_config.h>

#include "arm_internal.h"
#include "hardware/sam_pwm.h"
#include "hardware/sam_tc.h"
#include "hardware/sam_pmc.h"
#include "sam_gpio.h"
#include "sam_periphclks.h"

/* IRQ header for peripheral IDs */
#include <arch/chip/irq.h>

/* PWMC register offsets - channel registers */
#define PWM_CMR_OFFSET      0x200   /* Channel Mode Register base */
#define PWM_CDTY_OFFSET     0x204   /* Channel Duty Cycle Register base */
#define PWM_CDTYUPD_OFFSET  0x208   /* Channel Duty Cycle Update Register base */
#define PWM_CPRD_OFFSET     0x20C   /* Channel Period Register base */
#define PWM_CPRDUPD_OFFSET  0x210   /* Channel Period Update Register base */
#define PWM_CCNT_OFFSET     0x214   /* Channel Counter Register base */

/* Channel spacing in register map */
#define PWM_CHAN_SPACING    0x20    /* 32 bytes per channel */

/* Global PWMC registers */
#define PWM_CLK_OFFSET      0x000   /* Clock Register */
#define PWM_ENA_OFFSET      0x004   /* Enable Register */
#define PWM_DIS_OFFSET      0x008   /* Disable Register */
#define PWM_SR_OFFSET       0x00C   /* Status Register */
#define PWM_SCM_OFFSET      0x020   /* Sync Channels Mode Register */
#define PWM_SCUC_OFFSET     0x028   /* Sync Channels Update Control Register */
#define PWM_SCUP_OFFSET     0x02C   /* Sync Channels Update Period Register */
#define PWM_IER2_OFFSET     0x034   /* Interrupt Enable Register 2 */
#define PWM_IDR2_OFFSET     0x038   /* Interrupt Disable Register 2 */
#define PWM_ISR2_OFFSET     0x040   /* Interrupt Status Register 2 */
#define PWM_SCM_PTRM   (1u << 20)   // check header if defined

/* Clock configuration:
 * MCK = 150MHz
 *
 * CPRD register is 16-bit (max 65535), so prescaler must be chosen based on rate:
 *   - For rates >= 286 Hz: MCK/8  (18.75 MHz) → CPRD fits in 16-bit
 *   - For rates < 286 Hz:  MCK/64 (2.34 MHz)  → CPRD fits in 16-bit
 *
 * Example calculations:
 *   400 Hz @ MCK/8:  18750000 / 400 = 46875  ✓ fits
 *   50 Hz  @ MCK/8:  18750000 / 50  = 375000 ✗ OVERFLOW!
 *   50 Hz  @ MCK/64: 2343750  / 50  = 46875  ✓ fits
 */
#define PWM_DEFAULT_RATE        400
#define MCK_FREQUENCY           150000000UL
#define CPRD_MAX                65535       /* 16-bit register max */

/* Prescaler options (CPRE field in CMR register) */
#define PWM_CPRE_MCK8           3           /* MCK/8  = 18.75 MHz */
#define PWM_CPRE_MCK64          6           /* MCK/64 = 2.34375 MHz */

#define PWM_CLK_MCK8            (MCK_FREQUENCY / 8)     /* 18,750,000 Hz */
#define PWM_CLK_MCK64           (MCK_FREQUENCY / 64)    /* 2,343,750 Hz */

/* Threshold rate: below this, use MCK/64; at or above, use MCK/8
 * MCK/8 minimum rate = 18750000 / 65535 = 286.10 Hz
 * At 286 Hz: CPRD = 65559 > 65535 (overflow!)
 * At 287 Hz: CPRD = 65331 < 65535 (fits)
 * Compute dynamically to avoid hard-coding errors.
 */
#define PWM_RATE_THRESHOLD      ((PWM_CLK_MCK8 / CPRD_MAX) + 1)  /* = 287 */

/* Channel Mode Register (CMR) settings:
 * CPRE: Selected dynamically based on rate
 * CALG = 0: Left-aligned (edge-aligned)
 * CPOL = 1: Output starts high, goes low at duty match (normal polarity for ESCs)
 */
#define PWM_CMR_CPRE_SHIFT      0
#define PWM_CMR_CPRE_MASK       (0xF << PWM_CMR_CPRE_SHIFT)
#define PWM_CMR_CPOL            (1 << 9)    /* Channel polarity: high at start */

#define PWM_CPRE_MCK2           1           /* MCK/2 = 75 MHz for DShot */
#define PWM_DSHOT_SYNC_MASK     0x0f
#define PWM_SCM_UPDM_MODE2      (2u << 16)
#define PWM_SCUC_UPDULOCK       (1u << 0)
#define PWM_IR2_WRDY            (1u << 0)
#define PWM_IR2_UNRE            (1u << 3)


/* TC waveform register offsets (aliases to NuttX names from hardware/sam_tc.h) */
#define TC_CCR_OFF   SAM_TC_CCR_OFFSET   /* 0x0000 */
#define TC_CMR_OFF   SAM_TC_CMR_OFFSET   /* 0x0004 */
#define TC_RA_OFF    SAM_TC_RA_OFFSET    /* 0x0014 */
#define TC_RB_OFF    SAM_TC_RB_OFFSET    /* 0x0018 */
#define TC_RC_OFF    SAM_TC_RC_OFFSET    /* 0x001c */
#define TC_IDR_OFF   SAM_TC_IDR_OFFSET
/* TC_CCR_CLKEN/CLKDIS/SWTRG and TC_CMR_* come from hardware/sam_tc.h (already included above) */

  static inline bool timer_is_tc(unsigned timer) {
      for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++)
          if (timer_io_channels[ch].timer_index == timer)
              return timer_io_channels[ch].is_tc != 0;
      return false;
  }

/* Channel state tracking */
static io_timer_channel_mode_t g_channel_modes[MAX_TIMER_IO_CHANNELS];
static bool g_timers_initialized[MAX_IO_TIMERS];
static io_timer_channel_mode_t g_timer_modes[MAX_IO_TIMERS]; /* Mode allocated for each timer */
static uint32_t g_timer_period[MAX_IO_TIMERS];  /* CPRD value for each timer */
static uint32_t g_timer_clock[MAX_IO_TIMERS];   /* Clock frequency for each timer */
static uint8_t  g_timer_cpre[MAX_IO_TIMERS];    /* Prescaler (CPRE) for each timer */
static uint32_t g_dshot_channel_mask[MAX_IO_TIMERS];
static uint32_t g_dshot_reset_duty[MAX_IO_TIMERS];

/* Channel handler callbacks (for capture/DShot IRQ dispatch — wired in Phase 1C) */
static channel_handler_t g_channel_handler_callbacks[MAX_TIMER_IO_CHANNELS];
static void             *g_channel_handler_contexts[MAX_TIMER_IO_CHANNELS];

/* Enable peripheral clock for PWMC module via PMC
 * PWM0: PID 31 -> PCER0 bit 31
 * PWM1: PID 60 -> PCER1 bit 28 (60 - 32)
 */
  static void enable_pwm_clock(unsigned timer)
  {
      if (timer_is_tc(timer)) {
          putreg32(io_timers[timer].clock_bit, io_timers[timer].clock_register);
          return;
      }
      if (timer == 0) { putreg32((1 << SAM_PID_PWM0), SAM_PMC_PCER0); }
      else if (timer == 1) { putreg32((1 << (SAM_PID_PWM1-32)), SAM_PMC_PCER1); }
  }


/* Helper to get PWMC base address for a timer (PWM module) */
static inline uint32_t get_pwm_base(unsigned timer)
{
	if (timer >= MAX_IO_TIMERS) {
		return 0;
	}

	return io_timers[timer].base;
}

/* Helper to get channel register address
 * channel = index into timer_io_channels[]
 * Returns the base address for channel-specific registers
 */
static inline uint32_t get_channel_reg_base(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;
	uint8_t pwm_ch = timer_io_channels[channel].timer_channel;  /* 0-3 */

	uint32_t base = get_pwm_base(timer_idx);

	if (base == 0) {
		return 0;
	}

	/* Channel registers start at offset 0x200, spacing of 0x20 per channel */
	return base + PWM_CMR_OFFSET + (pwm_ch * PWM_CHAN_SPACING);
}

/* Register access helpers */
static inline void pwm_putreg(uint32_t addr, uint32_t value)
{
	putreg32(value, addr);
}

static inline uint32_t pwm_getreg(uint32_t addr)
{
	return getreg32(addr);
}

/* Channel-specific register helpers */
static inline void pwm_ch_putreg(uint32_t ch_base, uint32_t offset, uint32_t value)
{
	/* ch_base already points to CMR, adjust offset relative to CMR */
	putreg32(value, ch_base + (offset - PWM_CMR_OFFSET));
}

static inline uint32_t pwm_ch_getreg(uint32_t ch_base, uint32_t offset)
{
	return getreg32(ch_base + (offset - PWM_CMR_OFFSET));
}

/**
 * Select appropriate prescaler for requested rate
 * Returns prescaler value (CPRE) and sets clock frequency
 */
static uint8_t select_prescaler_for_rate(unsigned rate, uint32_t *clock_freq)
{
	if (rate >= PWM_RATE_THRESHOLD) {
		/* High rate: use MCK/8 for better resolution */
		*clock_freq = PWM_CLK_MCK8;
		return PWM_CPRE_MCK8;

	} else {
		/* Low rate (< 286 Hz): use MCK/64 to fit in 16-bit CPRD */
		*clock_freq = PWM_CLK_MCK64;
		return PWM_CPRE_MCK64;
	}
}

/**
 * Allocate a timer for a specific mode.
 * Prevents conflicting modes (e.g., PWM and DShot) on the same PWMC module.
 */
int io_timer_allocate_timer(unsigned timer, io_timer_channel_mode_t mode)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	irqstate_t flags = enter_critical_section();

	if (g_timers_initialized[timer] && g_timer_modes[timer] != mode) {
		leave_critical_section(flags);
		return -EBUSY;
	}

	g_timer_modes[timer] = mode;
	leave_critical_section(flags);

	return OK;
}

/**
 * Unallocate a timer, allowing reuse for a different mode.
 */
int io_timer_unallocate_timer(unsigned timer)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	g_timer_modes[timer] = IOTimerChanMode_NotUsed;
	g_timers_initialized[timer] = false;

	return OK;
}

/**
 * Initialize a timer block (PWMC module)
 */
int io_timer_init_timer(unsigned timer, io_timer_channel_mode_t mode)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	if (g_timers_initialized[timer]) {
		return OK;
	}

	/* Check/set timer allocation */
	int ret = io_timer_allocate_timer(timer, mode);

	if (ret != OK) {
		return ret;
	}

	/* Enable peripheral clock for this PWMC module */
	enable_pwm_clock(timer);
  if (timer_is_tc(timer)) {
          g_timer_clock[timer]  = io_timers[timer].clock_freq;
          g_timer_period[timer] = g_timer_clock[timer] / PWM_DEFAULT_RATE;
          g_timer_cpre[timer]   = 0;
          uint32_t base = io_timers[timer].base;
          putreg32(TC_CCR_CLKDIS, base + TC_CCR_OFF);
          /* Polarity matches PWMC (CPOL=1): output HIGH at period start, LOW at RA/RB match.
           * ACPA_CLEAR: TIOA goes LOW at RA compare (duty end)
           * ACPC_SET:   TIOA goes HIGH at RC compare (period reset = next period start)
           * BCPB_CLEAR: TIOB goes LOW at RB compare
           * BCPC_SET:   TIOB goes HIGH at RC compare
           */
          /* EEVT must be non-zero (XC0) so TIOB is free to drive as output.
           * Default EEVT=0 selects TIOB as external-event INPUT, which prevents
           * TIOB from being used as a PWM output (datasheet §47.7.2).
           */
          /* Inverted polarity: output LOW at period start, HIGH at RA compare.
           * This idles LOW naturally (RA=0 → no SET event → stays LOW).
           * DShot pulse: RA = (RC - duty) → LOW for (RC-duty), HIGH for duty.
           */
          uint32_t cmr = TC_CMR_TCCLKS_MCK8 | TC_CMR_WAVE | TC_CMR_WAVSEL_UPRC |
                         TC_CMR_EEVT_XC0 |
                         TC_CMR_ACPA_SET | TC_CMR_ACPC_CLEAR |
                         TC_CMR_BCPB_SET | TC_CMR_BCPC_CLEAR;
          putreg32(cmr,                        base + TC_CMR_OFF);
          putreg32(g_timer_period[timer],      base + TC_RC_OFF);
          putreg32(0,                          base + TC_RA_OFF);  /* RA=0 → idle LOW (no SET event) */
          putreg32(0,                          base + TC_RB_OFF);
          putreg32(TC_CCR_CLKEN | TC_CCR_SWTRG, base + TC_CCR_OFF);
          g_timers_initialized[timer] = true;
          return OK;
      }

	/* Select prescaler for default rate (400Hz uses MCK/8) */
	g_timer_cpre[timer] = select_prescaler_for_rate(PWM_DEFAULT_RATE, &g_timer_clock[timer]);

	/* Set default period for 400Hz PWM */
	g_timer_period[timer] = g_timer_clock[timer] / PWM_DEFAULT_RATE;

	PX4_DEBUG("PWMC Timer %u init: base=0x%08lx rate=%uHz cpre=%u clk=%luHz period=%lu",
		  timer, (unsigned long)io_timers[timer].base, PWM_DEFAULT_RATE, g_timer_cpre[timer],
		  (unsigned long)g_timer_clock[timer], (unsigned long)g_timer_period[timer]);

	g_timers_initialized[timer] = true;

	return OK;
}

/**
 * Initialize a PWM channel.
 *
 * Allocates the channel, initializes the parent timer block if needed,
 * configures HW registers, and stores the callback for IRQ dispatch.
 */
int io_timer_channel_init(unsigned channel, io_timer_channel_mode_t mode,
			  channel_handler_t channel_handler, void *context)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	if (mode <= IOTimerChanMode_NotUsed || mode >= IOTimerChanModeSize) {
		return -EINVAL;
	}

	/* Allocate the channel — returns -EBUSY if already in use */
	int ret = io_timer_allocate_channel(channel, mode);

	if (ret != OK) {
		return ret;
	}

	/* Store channel handler for IRQ dispatch (used by capture/DShot modes) */
	g_channel_handler_callbacks[channel] = channel_handler;
	g_channel_handler_contexts[channel] = context;

	uint8_t timer_idx = timer_io_channels[channel].timer_index;
	uint8_t pwm_ch = timer_io_channels[channel].timer_channel;

	/* Initialize the timer block if needed */
	ret = io_timer_init_timer(timer_idx, mode);

	if (ret != OK) {
		/* Timer init failed — release the channel allocation */
		g_channel_modes[channel] = IOTimerChanMode_NotUsed;
		return ret;
	}

	uint32_t base = get_pwm_base(timer_idx);
	uint32_t ch_base = get_channel_reg_base(channel);

	if (base == 0 || ch_base == 0) {
		g_channel_modes[channel] = IOTimerChanMode_NotUsed;
		return -EINVAL;
	}

	switch (mode) {
	case IOTimerChanMode_PWMOut: {
		uint32_t gpio = timer_io_channels[channel].gpio_out;

		/* Configure GPIO for output (peripheral A/B) */
		sam_configgpio(gpio);

		/* TC channels: ensure clock is running (may have been stopped by set_enable) */
		if (timer_io_channels[channel].is_tc) {
			uint32_t tc_base = io_timers[timer_idx].base;
			putreg32(TC_CCR_CLKEN | TC_CCR_SWTRG, tc_base + TC_CCR_OFF);
			break;
		}

		/* Disable channel first (write to DIS register) */
		pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));

		/* Configure Channel Mode Register:
		 * - CPRE: Selected prescaler for current rate
		 * - CALG = 0 (left-aligned)
		 * - CPOL = 1 (high at period start, low at duty match)
		 */
		uint32_t cmr = (g_timer_cpre[timer_idx] << PWM_CMR_CPRE_SHIFT) | PWM_CMR_CPOL;
		pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, cmr);

		/* Set period (CPRD register) - use direct register while channel disabled */
		pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, g_timer_period[timer_idx]);

		/* Set initial duty cycle to 0 (CDTY register)
		 * Note: With CPOL=1, CDTY=0 means output stays high entire period
		 * For motor safety, we want 0 duty initially
		 */
		pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, 0);

		/* Enable channel (write to ENA register) */
		pwm_putreg(base + PWM_ENA_OFFSET, (1 << pwm_ch));
		break;
	}

	case IOTimerChanMode_Dshot:
	case IOTimerChanMode_DshotInverted:
		/* DShot HW config done by dshot.c via io_timer_set_dshot_mode().
		 * Just configure GPIO for peripheral function here.
		 */
		sam_configgpio(timer_io_channels[channel].gpio_out);
		break;

	case IOTimerChanMode_Trigger:
	case IOTimerChanMode_Other:
		/* GPIO-only modes — drive pin as output-low until explicitly set.
		 * No PWMC HW configuration needed.
		 */
		sam_configgpio(io_timer_channel_get_gpio_output(channel));
		break;

	case IOTimerChanMode_Capture:
	case IOTimerChanMode_PWMIn:
	case IOTimerChanMode_PPS:
	case IOTimerChanMode_RPM:
	case IOTimerChanMode_CaptureDMA:
		/* TC-based capture modes — HW configured by input_capture.c (Phase 3).
		 * Channel is just reserved here.
		 */
		break;

	case IOTimerChanMode_OneShot:
		/* Same HW setup as PWMOut; trigger logic handled by io_timer_trigger() */
		sam_configgpio(timer_io_channels[channel].gpio_out);
		pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));

		uint32_t cmr = (g_timer_cpre[timer_idx] << PWM_CMR_CPRE_SHIFT) | PWM_CMR_CPOL;
		pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, cmr);
		pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, g_timer_period[timer_idx]);
		pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, 0);
		pwm_putreg(base + PWM_ENA_OFFSET, (1 << pwm_ch));
		break;

	case IOTimerChanMode_LED:
		/* LED PWM — same PWMC setup as PWMOut */
		sam_configgpio(timer_io_channels[channel].gpio_out);
		pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));

		uint32_t led_cmr = (g_timer_cpre[timer_idx] << PWM_CMR_CPRE_SHIFT) | PWM_CMR_CPOL;
		pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, led_cmr);
		pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, g_timer_period[timer_idx]);
		pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, 0);
		pwm_putreg(base + PWM_ENA_OFFSET, (1 << pwm_ch));
		break;

	default:
		break;
	}

	return OK;
}

/**
 * Set PWM rate (frequency) for a timer
 *
 * This function handles prescaler selection to ensure the period fits in the
 * 16-bit CPRD register. When changing prescaler, channels must be disabled
 * and re-enabled for the CMR change to take effect.
 */
int io_timer_set_rate(unsigned timer, unsigned rate)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	if (rate < 50 || rate > 8000) {
		return -EINVAL;
	}

	/* TC timer: RC = clock_freq / rate, no prescaler change needed */
	if (timer_is_tc(timer)) {
		uint32_t period = g_timer_clock[timer] / rate;
		g_timer_period[timer] = period;
		putreg32(period, io_timers[timer].base + TC_RC_OFF);
		return OK;
	}

	/* Select appropriate prescaler for this rate */
	uint32_t new_clock;
	uint8_t new_cpre = select_prescaler_for_rate(rate, &new_clock);

	/* Calculate new period */
	uint32_t period = new_clock / rate;

	/* Sanity check: period must fit in 16-bit register */
	if (period > CPRD_MAX) {
		PX4_ERR("PWMC Timer %u: period %lu overflow for rate %u (cpre=%u clk=%lu)",
			timer, (unsigned long)period, rate, new_cpre, (unsigned long)new_clock);
		return -EINVAL;
	}

	bool prescaler_changed = (new_cpre != g_timer_cpre[timer]);

	/* Store new values */
	g_timer_clock[timer] = new_clock;
	g_timer_cpre[timer] = new_cpre;
	g_timer_period[timer] = period;

	PX4_DEBUG("PWMC Timer %u: rate=%uHz cpre=%u clk=%luHz period=%lu%s",
		  timer, rate, new_cpre, (unsigned long)new_clock, (unsigned long)period,
		  prescaler_changed ? " (prescaler changed)" : "");

	uint32_t base = get_pwm_base(timer);

	/* Update all channels using this timer */
	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if (timer_io_channels[ch].timer_index == timer &&
		    g_channel_modes[ch] == IOTimerChanMode_PWMOut) {

			uint8_t pwm_ch = timer_io_channels[ch].timer_channel;
			uint32_t ch_base = get_channel_reg_base(ch);

			if (prescaler_changed) {
				/* Prescaler changed: must disable channel, update CMR, re-enable */
				pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));

				/* Wait for channel to be disabled - check SR bit
				 * CRITICAL: Channel finishes current period before disabling!
				 * At 400Hz, period = 2.5ms. At 50Hz, period = 20ms.
				 * Must wait long enough for the period to complete.
				 */
				uint32_t sr = pwm_getreg(base + PWM_SR_OFFSET);
				int timeout_ms = 50;  /* 50ms should be enough for any PWM period */

				while ((sr & (1 << pwm_ch)) && timeout_ms > 0) {
					up_udelay(1000);  /* 1ms delay */
					sr = pwm_getreg(base + PWM_SR_OFFSET);
					timeout_ms--;
				}

				if (timeout_ms == 0) {
					PX4_ERR("PWMC Ch %u: timeout waiting for disable (SR=0x%08lx)",
						ch, (unsigned long)sr);
				}

				/* Update CMR with new prescaler */
				uint32_t cmr = (new_cpre << PWM_CMR_CPRE_SHIFT) | PWM_CMR_CPOL;
				pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, cmr);

				/* Write CPRD directly (channel is disabled) */
				pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, period);

				/* Re-enable channel */
				pwm_putreg(base + PWM_ENA_OFFSET, (1 << pwm_ch));

			} else {
				/* Same prescaler: use buffered update register */
				pwm_ch_putreg(ch_base, PWM_CPRDUPD_OFFSET, period);
			}
		}
	}

	return OK;
}

/**
 * Enable/disable PWM channels.
 *
 * When masks == IO_TIMER_ALL_MODES_CHANNELS (0), all channels currently
 * allocated to the given mode are affected.  Otherwise the caller's mask
 * is intersected with the mode's channels (safety filter).
 */
int io_timer_set_enable(bool state, io_timer_channel_mode_t mode,
			io_timer_channel_allocation_t masks)
{
	if (masks == IO_TIMER_ALL_MODES_CHANNELS) {
		/* Caller wants all channels in this mode */
		masks = io_timer_get_mode_channels(mode);

	} else {
		/* Only affect channels that are actually in the requested mode */
		masks &= io_timer_get_mode_channels(mode);
	}

	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if (masks & (1 << ch)) {
			uint8_t timer_idx = timer_io_channels[ch].timer_index;

			if (timer_io_channels[ch].is_tc) {
				uint32_t tc_base = io_timers[timer_idx].base;
				putreg32(state ? (TC_CCR_CLKEN | TC_CCR_SWTRG) : TC_CCR_CLKDIS,
					 tc_base + TC_CCR_OFF);
				continue;
			}

			uint8_t pwm_ch = timer_io_channels[ch].timer_channel;
			uint32_t base = get_pwm_base(timer_idx);

			if (state) {
				pwm_putreg(base + PWM_ENA_OFFSET, (1 << pwm_ch));
			} else {
				pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));
			}
		}
	}

	return OK;
}

/**
 * Set PWM duty cycle (CCR = Compare Capture Register, here CDTY)
 * Value is in microseconds (1000-2000 typical for servos/ESCs)
 */
int io_timer_set_ccr(unsigned channel, uint16_t value)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	if (g_channel_modes[channel] != IOTimerChanMode_PWMOut) {
		return -EINVAL;
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;

	/* Convert microseconds to timer ticks */
	uint32_t ticks = (uint64_t)value * g_timer_clock[timer_idx] / 1000000ULL;

	if (ticks > g_timer_period[timer_idx]) {
		ticks = g_timer_period[timer_idx];
	}

	/* TC channels: write RA (TIOA) or RB (TIOB) based on ccr_offset */
	if (timer_io_channels[channel].is_tc) {
		uint32_t tc_base = io_timers[timer_idx].base;
		putreg32(ticks, tc_base + timer_io_channels[channel].ccr_offset);
		return OK;
	}

	/* PWMC: use CDTYUPD for glitch-free update at period boundary */
	uint32_t ch_base = get_channel_reg_base(channel);
	pwm_ch_putreg(ch_base, PWM_CDTYUPD_OFFSET, ticks);

	return OK;
}

/**
 * Get current CCR (duty cycle) value
 */
uint16_t io_channel_get_ccr(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;
	uint32_t ticks;

	if (timer_io_channels[channel].is_tc) {
		uint32_t tc_base = io_timers[timer_idx].base;
		ticks = getreg32(tc_base + timer_io_channels[channel].ccr_offset);
	} else {
		uint32_t ch_base = get_channel_reg_base(channel);
		ticks = pwm_ch_getreg(ch_base, PWM_CDTY_OFFSET);
	}

	return (uint16_t)((uint64_t)ticks * 1000000ULL / g_timer_clock[timer_idx]);
}

/**
 * Get channel group bitmask for a timer
 */
uint32_t io_timer_get_group(unsigned timer)
{
	if (timer >= MAX_IO_TIMERS) {
		return 0;
	}

	uint32_t mask = 0;

	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if (timer_io_channels[ch].timer_index == timer) {
			mask |= (1 << ch);
		}
	}

	return mask;
}

/**
 * Validate channel index
 */
int io_timer_validate_channel_index(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	return OK;
}

/**
 * Check if channel is free
 */
int io_timer_is_channel_free(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	return g_channel_modes[channel] == IOTimerChanMode_NotUsed ? 0 : -EBUSY;
}

/**
 * Free a channel
 */
int io_timer_free_channel(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;
	io_timer_channel_mode_t old_mode = g_channel_modes[channel];

	if (timer_io_channels[channel].is_tc) {
		/* TC: set duty to 0, leave clock running — do NOT write PWMC DIS register */
		uint32_t tc_base = io_timers[timer_idx].base;
		putreg32(0, tc_base + timer_io_channels[channel].ccr_offset);
		g_channel_modes[channel] = IOTimerChanMode_NotUsed;
		return OK;
	}

	uint8_t pwm_ch = timer_io_channels[channel].timer_channel;
	uint32_t base = get_pwm_base(timer_idx);

	pwm_putreg(base + PWM_DIS_OFFSET, (1 << pwm_ch));

	if (old_mode == IOTimerChanMode_Dshot || old_mode == IOTimerChanMode_DshotInverted) {
		g_dshot_channel_mask[timer_idx] &= ~(1u << pwm_ch);
	}

	g_channel_modes[channel] = IOTimerChanMode_NotUsed;

	return OK;
}

/**
 * Get channel mode
 */
int io_timer_get_channel_mode(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return IOTimerChanMode_NotUsed;
	}

	return g_channel_modes[channel];
}

/**
 * Get bitmask of channels in a specific mode
 */
int io_timer_get_mode_channels(io_timer_channel_mode_t mode)
{
	int mask = 0;

	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if (g_channel_modes[ch] == mode) {
			mask |= (1 << ch);
		}
	}

	return mask;
}

/**
 * Get GPIO input configuration for a channel pin.
 *
 * Returns gpio_in if the board explicitly configured an input alternate
 * function (e.g., TC capture).  Otherwise derives a GPIO input config
 * from the output pin's port/pin so that PX4_MAKE_GPIO_EXTI and
 * px4_arch_gpiosetevent get a valid pin identifier.
 */
uint32_t io_timer_channel_get_as_pwm_input(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	if (timer_io_channels[channel].gpio_in != 0) {
		return timer_io_channels[channel].gpio_in;
	}

	/* Fallback: derive input config from output pin's port/pin */
	uint32_t pin_id = timer_io_channels[channel].gpio_out
			  & (GPIO_PORT_MASK | GPIO_PIN_MASK);

	return GPIO_INPUT | GPIO_CFG_PULLUP | pin_id;
}

/**
 * Allocate a channel for a specific mode.
 * Returns 0 on success, -EBUSY if already allocated.
 */
int io_timer_allocate_channel(unsigned channel, io_timer_channel_mode_t mode)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	irqstate_t flags = enter_critical_section();

	if (g_channel_modes[channel] != IOTimerChanMode_NotUsed) {
		leave_critical_section(flags);
		return -EBUSY;
	}

	g_channel_modes[channel] = mode;
	leave_critical_section(flags);

	return OK;
}

/**
 * Unallocate a channel (alias for io_timer_free_channel)
 */
int io_timer_unallocate_channel(unsigned channel)
{
	return io_timer_free_channel(channel);
}

/**
 * Get GPIO output-low configuration for a channel pin.
 *
 * Extracts port/pin from the peripheral mux config and returns a
 * plain GPIO_OUTPUT | GPIO_OUTPUT_CLEAR configuration.  This is used
 * by board_on_reset(), camera_trigger, and DShot idle-state paths
 * that need to drive the pin as a generic digital output (low).
 */
uint32_t io_timer_channel_get_gpio_output(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	uint32_t pin_id = timer_io_channels[channel].gpio_out
			  & (GPIO_PORT_MASK | GPIO_PIN_MASK);

	return GPIO_OUTPUT | GPIO_CFG_DEFAULT | GPIO_OUTPUT_CLEAR | pin_id;
}

/**
 * Set PWM rate for a timer (alias for io_timer_set_rate)
 */
int io_timer_set_pwm_rate(unsigned timer, unsigned rate)
{
	return io_timer_set_rate(timer, rate);
}

/**
 * Trigger PWM update on specified channels
 * For PWMC with CDTYUPD/CPRDUPD, updates are automatic at period boundary.
 * This function is a no-op for synchronous mode.
 */
void io_timer_trigger(unsigned channels_mask)
{
	/* PWMC channels using CDTYUPD update automatically at the next period
	 * boundary. No explicit trigger needed unlike some STM32 implementations.
	 */
	(void)channels_mask;
}

void io_timer_set_dshot_channel_mask(uint8_t timer, uint32_t mask)
{
	if (timer < MAX_IO_TIMERS) {
		g_dshot_channel_mask[timer] = mask & PWM_DSHOT_SYNC_MASK;
	}
}

void io_timer_dshot_force_low(uint8_t timer)
{
	if (timer >= MAX_IO_TIMERS) {
		return;
	}

	if (timer_is_tc(timer)) {
		uint32_t base = io_timers[timer].base;

		for (unsigned output = 0; output < MAX_TIMER_IO_CHANNELS; output++) {
			if (timer_io_channels[output].timer_index == timer && timer_io_channels[output].is_tc) {
				putreg32(0, base + timer_io_channels[output].ccr_offset);
			}
		}

		return;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		return;
	}

	for (uint8_t pwm_ch = 0; pwm_ch < 4; pwm_ch++) {
		if (g_dshot_channel_mask[timer] & (1u << pwm_ch)) {
			uint32_t ch_base = base + PWM_CMR_OFFSET + (pwm_ch * PWM_CHAN_SPACING);
			pwm_ch_putreg(ch_base, PWM_CDTYUPD_OFFSET, g_dshot_reset_duty[timer]);
			pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, g_dshot_reset_duty[timer]);
		}
	}

	pwm_putreg(base + PWM_SCUC_OFFSET, PWM_SCUC_UPDULOCK);
}

bool io_timer_dshot_check_unre(uint8_t timer)
{
	if (timer >= MAX_IO_TIMERS || timer_is_tc(timer)) {
		return false;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		return false;
	}

	return (pwm_getreg(base + PWM_ISR2_OFFSET) & PWM_IR2_UNRE) != 0;
}

void io_timer_dshot_debug_dump(uint8_t timer)
{
	if (timer >= MAX_IO_TIMERS || timer_is_tc(timer)) {
		PX4_INFO("DShot debug timer %u invalid/TC", timer);
		return;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		PX4_INFO("DShot debug timer %u base=0", timer);
		return;
	}

	PX4_INFO("PWMC%u base=0x%08lx SR=0x%08lx SCM=0x%08lx ISR2=0x%08lx mask=0x%lx",
		 timer, (unsigned long)base,
		 (unsigned long)pwm_getreg(base + PWM_SR_OFFSET),
		 (unsigned long)pwm_getreg(base + PWM_SCM_OFFSET),
		 (unsigned long)pwm_getreg(base + PWM_ISR2_OFFSET),
		 (unsigned long)g_dshot_channel_mask[timer]);

	for (uint8_t pwm_ch = 0; pwm_ch < 4; pwm_ch++) {
		uint32_t ch_base = base + PWM_CMR_OFFSET + (pwm_ch * PWM_CHAN_SPACING);
		PX4_INFO("  CH%u CMR=0x%08lx CDTY=%lu CPRD=%lu CCNT=%lu",
			 pwm_ch,
			 (unsigned long)pwm_ch_getreg(ch_base, PWM_CMR_OFFSET),
			 (unsigned long)pwm_ch_getreg(ch_base, PWM_CDTY_OFFSET),
			 (unsigned long)pwm_ch_getreg(ch_base, PWM_CPRD_OFFSET),
			 (unsigned long)pwm_ch_getreg(ch_base, PWM_CCNT_OFFSET));
	}
}

int io_timer_dshot_debug_pwm(uint8_t timer, unsigned rate_hz, unsigned duty_percent)
{
	if (timer >= MAX_IO_TIMERS || timer_is_tc(timer)) {
		return -EINVAL;
	}

	if (rate_hz == 0 || duty_percent > 100) {
		return -EINVAL;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		return -EINVAL;
	}

	uint32_t clock = MCK_FREQUENCY / 64u;
	uint32_t cprd = clock / rate_hz;

	if (cprd == 0 || cprd > CPRD_MAX) {
		return -ERANGE;
	}

	uint32_t duty = (cprd * duty_percent) / 100u;
	uint32_t hw_mask = 0;

	enable_pwm_clock(timer);
	pwm_putreg(base + PWM_IDR2_OFFSET, PWM_IR2_WRDY);
	pwm_putreg(base + PWM_DIS_OFFSET, PWM_DSHOT_SYNC_MASK);

	for (unsigned output = 0; output < MAX_TIMER_IO_CHANNELS; output++) {
		if (timer_io_channels[output].timer_index != timer || timer_io_channels[output].is_tc) {
			continue;
		}

		uint8_t pwm_ch = timer_io_channels[output].timer_channel;

		if (pwm_ch >= 4) {
			continue;
		}

		sam_configgpio(timer_io_channels[output].gpio_out);
		hw_mask |= (1u << pwm_ch);
	}

	if (hw_mask == 0) {
		return -EINVAL;
	}

	for (uint8_t pwm_ch = 0; pwm_ch < 4; pwm_ch++) {
		uint32_t ch_base = base + PWM_CMR_OFFSET + (pwm_ch * PWM_CHAN_SPACING);
		pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, PWM_CPRE_MCK64 | PWM_CMR_CPOL);
		pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, cprd);
		pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, duty);
	}

	/* Disable sync/DMA mode for this direct hardware test. */
	pwm_putreg(base + PWM_SCM_OFFSET, 0);
	pwm_putreg(base + PWM_ENA_OFFSET, hw_mask);

	g_timer_cpre[timer] = PWM_CPRE_MCK64;
	g_timer_clock[timer] = clock;
	g_timer_period[timer] = cprd;

	PX4_INFO("PWMC%u hwtest mask=0x%lx rate=%u duty=%u%% cprd=%lu cdty=%lu",
		 timer, (unsigned long)hw_mask, rate_hz, duty_percent,
		 (unsigned long)cprd, (unsigned long)duty);
	io_timer_dshot_debug_dump(timer);

	return OK;
}

void io_timer_update_dma_req(uint8_t timer, bool enable)
{
	if (timer >= MAX_IO_TIMERS || timer_is_tc(timer)) {
		return;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		return;
	}

	if (enable) {
		(void)pwm_getreg(base + PWM_ISR2_OFFSET);
		pwm_putreg(base + PWM_IER2_OFFSET, PWM_IR2_WRDY);
	} else {
		pwm_putreg(base + PWM_IDR2_OFFSET, PWM_IR2_WRDY);
	}
}

int io_timer_set_dshot_mode(uint8_t timer, unsigned dshot_pwm_freq)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	if (dshot_pwm_freq == 0) {
		return -EINVAL;
	}

	if (timer_is_tc(timer)) {
		uint32_t base = io_timers[timer].base;
		uint32_t cprd = io_timers[timer].clock_freq / dshot_pwm_freq;

		if (base == 0 || cprd == 0 || cprd > CPRD_MAX) {
			return -ERANGE;
		}

		enable_pwm_clock(timer);
		putreg32(TC_INT_CPCS, base + TC_IDR_OFF);
		putreg32(TC_CCR_CLKDIS, base + TC_CCR_OFF);

		uint32_t cmr = TC_CMR_TCCLKS_MCK8 | TC_CMR_WAVE | TC_CMR_WAVSEL_UPRC |
			       TC_CMR_EEVT_XC0 |
			       TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET |
			       TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_SET;
		putreg32(cmr, base + TC_CMR_OFF);
		putreg32(cprd, base + TC_RC_OFF);
		putreg32(0, base + TC_RA_OFF);
		putreg32(0, base + TC_RB_OFF);

		g_timer_clock[timer] = io_timers[timer].clock_freq;
		g_timer_period[timer] = cprd;
		g_timer_cpre[timer] = 0;
		g_dshot_reset_duty[timer] = 0;

		PX4_INFO("DShot TC timer %u mask=0x%lx freq=%u rc=%lu",
			 timer, (unsigned long)g_dshot_channel_mask[timer], dshot_pwm_freq, (unsigned long)cprd);

		return OK;
	}

	uint32_t base = get_pwm_base(timer);

	if (base == 0) {
		return -EINVAL;
	}

	uint32_t cprd = (MCK_FREQUENCY / 2u) / dshot_pwm_freq;

	if (cprd == 0 || cprd > CPRD_MAX) {
		return -ERANGE;
	}

	uint32_t channel_mask = g_dshot_channel_mask[timer] & PWM_DSHOT_SYNC_MASK;

	if (channel_mask == 0) {
		return -EINVAL;
	}

	enable_pwm_clock(timer);

	pwm_putreg(base + PWM_IDR2_OFFSET, PWM_IR2_WRDY);
	pwm_putreg(base + PWM_DIS_OFFSET, PWM_DSHOT_SYNC_MASK);

	uint32_t sr = pwm_getreg(base + PWM_SR_OFFSET);
	int timeout_us = 50000;

	while ((sr & PWM_DSHOT_SYNC_MASK) && timeout_us > 0) {
		up_udelay(10);
		timeout_us -= 10;
		sr = pwm_getreg(base + PWM_SR_OFFSET);
	}

	if (sr & PWM_DSHOT_SYNC_MASK) {
		PX4_ERR("DShot PWMC timer %u disable timeout SR=0x%08lx", timer, (unsigned long)sr);
		return -ETIMEDOUT;
	}

	g_timer_cpre[timer] = PWM_CPRE_MCK2;
	g_timer_clock[timer] = MCK_FREQUENCY / 2u;
	g_timer_period[timer] = cprd;
	g_dshot_reset_duty[timer] = 0;

	for (uint8_t pwm_ch = 0; pwm_ch < 4; pwm_ch++) {
		uint32_t ch_base = base + PWM_CMR_OFFSET + (pwm_ch * PWM_CHAN_SPACING);

		/* CPOL=1 on SAMV71 PWMH gives active-high pulses: high at period
		 * start, then low after CDTY ticks. CDTY=0 was verified by the
		 * current board symptom to idle low, so reset duty is 0.
		 */
		pwm_ch_putreg(ch_base, PWM_CMR_OFFSET, PWM_CPRE_MCK2 | PWM_CMR_CPOL);
		pwm_ch_putreg(ch_base, PWM_CPRD_OFFSET, cprd);
		pwm_ch_putreg(ch_base, PWM_CDTY_OFFSET, g_dshot_reset_duty[timer]);
	}

	/* SCM: Synchronous Channel Mode with DMA (UPDM=2).
	 * All 4 PWMC channels synchronized to CH0 period.
	 * XDMAC writes duty values to PWM_DMAR; hardware distributes
	 * to each channel's CDTYUPD at each period boundary.
	 */
	pwm_putreg(base + PWM_SCM_OFFSET,
		   SCM_SYNC_SEL(channel_mask) | SCM_UPDM_MODE2);

	/* SCUP: Update period = 0 (update every period) */
	pwm_putreg(base + PWM_SCUP_OFFSET, 0);

	/* Channels stay DISABLED here — up_dshot_arm() will enable later */

	PX4_INFO("DShot PWMC timer %u mask=0x%lx freq=%u cprd=%lu SCM=UPDM2+DMA",
		 timer, (unsigned long)channel_mask, dshot_pwm_freq, (unsigned long)cprd);

	return OK;
}
