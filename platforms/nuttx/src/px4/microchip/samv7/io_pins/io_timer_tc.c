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
 * @file io_timer_tc.c
 *
 * DEPRECATED: TC-based PWM replaced by PWMC (io_timer_pwmc.c).
 * Retained for reference only. Do not add to CMakeLists.
 *
 * SAMV7 IO Timer implementation using TC (Timer/Counter) in waveform mode.
 *
 * PWM generation using TC waveform mode:
 * - WAVSEL = 2 (UP mode with trigger on RC compare)
 * - RC register = period (determines PWM frequency)
 * - RA register = duty cycle threshold
 * - ACPA = SET (set TIOA on RA match)
 * - ACPC = CLEAR (clear TIOA on RC match)
 */

#include <px4_platform_common/px4_config.h>
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
#include "hardware/sam_tc.h"
#include "hardware/sam_pmc.h"
#include "sam_gpio.h"
#include "sam_periphclks.h"

/* Include IRQ header for SAM_PID_TCx definitions
 * TC block 0: channels TC0, TC1, TC2 (PIDs 23, 24, 25)
 * TC block 1: channels TC3, TC4, TC5 (PIDs 26, 27, 28)
 */
#include <arch/chip/irq.h>

/* TC register offsets within a channel */
#define TC_CCR_OFFSET   0x00
#define TC_CMR_OFFSET   0x04
#define TC_CV_OFFSET    0x10
#define TC_RA_OFFSET    0x14
#define TC_RB_OFFSET    0x18
#define TC_RC_OFFSET    0x1C
#define TC_SR_OFFSET    0x20
#define TC_IER_OFFSET   0x24
#define TC_IDR_OFFSET   0x28
#define TC_IMR_OFFSET   0x2C

/* Channel offset within TC block */
#define TC_CHAN_SIZE    0x40

/* Use NuttX TC register definitions from hardware/sam_tc.h */
/* TC_CMR_TCCLKS_*, TC_CMR_WAVE, TC_CMR_WAVSEL_*, TC_CMR_ACPA_*, TC_CMR_ACPC_* */
/* TC_CCR_CLKEN, TC_CCR_CLKDIS, TC_CCR_SWTRG */

/* Default PWM frequency 400Hz, period in timer ticks */
/* With MCK=150MHz, MCK/8 = 18.75MHz, for 400Hz: RC = 18750000/400 = 46875 */
#define PWM_DEFAULT_RATE        400
#define TC_CLOCK_DIVIDER        8
#define MCK_FREQUENCY           150000000UL
#define TC_CLOCK_FREQ           (MCK_FREQUENCY / TC_CLOCK_DIVIDER)

/* Channel state tracking */
static io_timer_channel_mode_t g_channel_modes[MAX_TIMER_IO_CHANNELS];
static bool g_timers_initialized[MAX_IO_TIMERS];
static uint32_t g_timer_period[MAX_IO_TIMERS];  /* RC value for each timer */

/* Get TC channel peripheral ID based on timer index
 * Timer1 = TC0 CH1 = PID 24
 * Timer2 = TC0 CH2 = PID 25
 * Timer3 = TC1 CH0 = PID 26
 * Timer4 = TC1 CH1 = PID 27
 */
static inline uint32_t get_tc_pid(unsigned timer)
{
	switch (timer) {
	case 0: return SAM_PID_TC1;  /* Timer1 -> TC0 CH1 */
	case 1: return SAM_PID_TC2;  /* Timer2 -> TC0 CH2 */
	case 2: return SAM_PID_TC3;  /* Timer3 -> TC1 CH0 */
	case 3: return SAM_PID_TC4;  /* Timer4 -> TC1 CH1 */
	default: return 0;
	}
}

/* Enable peripheral clock for a TC channel */
static void enable_tc_clock(unsigned timer)
{
	uint32_t pid = get_tc_pid(timer);

	if (pid == 0) {
		return;
	}

	/* PIDs 0-31 use PCER0 */
	uint32_t regval = getreg32(SAM_PMC_PCER0);
	regval |= (1 << pid);
	putreg32(regval, SAM_PMC_PCER0);
}

/* Helper to get channel base address */
static inline uint32_t get_channel_base(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;
	uint8_t timer_ch = timer_io_channels[channel].timer_channel;

	return io_timers[timer_idx].base + (timer_ch * TC_CHAN_SIZE);
}

/* Register access helpers */
static inline void tc_putreg(uint32_t base, uint32_t offset, uint32_t value)
{
	putreg32(value, base + offset);
}

static inline uint32_t tc_getreg(uint32_t base, uint32_t offset)
{
	return getreg32(base + offset);
}

/**
 * Initialize a timer block
 */
int io_timer_init_timer(unsigned timer)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	if (g_timers_initialized[timer]) {
		return OK;
	}

	/* Enable peripheral clock for this TC channel via PMC */
	enable_tc_clock(timer);

	/* Set default period for 400Hz PWM */
	g_timer_period[timer] = TC_CLOCK_FREQ / PWM_DEFAULT_RATE;

	g_timers_initialized[timer] = true;

	return OK;
}

/**
 * Initialize a PWM channel
 */
int io_timer_channel_init(unsigned channel, io_timer_channel_mode_t mode,
			  channel_handler_t channel_handler, void *context)
{
	(void)channel_handler;
	(void)context;

	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	if (mode != IOTimerChanMode_PWMOut && mode != IOTimerChanMode_NotUsed) {
		return -EINVAL;  /* Only PWM output supported for now */
	}

	uint8_t timer_idx = timer_io_channels[channel].timer_index;

	/* Initialize the timer block if needed */
	int ret = io_timer_init_timer(timer_idx);

	if (ret != OK) {
		return ret;
	}

	uint32_t base = get_channel_base(channel);

	if (base == 0) {
		return -EINVAL;
	}

	if (mode == IOTimerChanMode_PWMOut) {
		uint32_t gpio = timer_io_channels[channel].gpio_out;

		/* Configure GPIO for TC output */
		sam_configgpio(gpio);

		/* Disable channel clock first */
		tc_putreg(base, TC_CCR_OFFSET, TC_CCR_CLKDIS);

		/* Configure for waveform mode:
		 * - Clock source: MCK/8
		 * - Waveform mode enabled
		 * - UP mode with trigger on RC compare
		 * - RA compare sets TIOA
		 * - RC compare clears TIOA
		 */
		uint32_t cmr = TC_CMR_TCCLKS_MCK8 |
			       TC_CMR_WAVE |
			       TC_CMR_WAVSEL_UPRC |
			       TC_CMR_ACPA_SET |
			       TC_CMR_ACPC_CLEAR;
		tc_putreg(base, TC_CMR_OFFSET, cmr);

		/* Set period (RC register) */
		tc_putreg(base, TC_RC_OFFSET, g_timer_period[timer_idx]);

		/* Set initial duty cycle to 0 (RA register) */
		tc_putreg(base, TC_RA_OFFSET, 0);

		/* Enable channel clock and trigger */
		tc_putreg(base, TC_CCR_OFFSET, TC_CCR_CLKEN | TC_CCR_SWTRG);
	}

	g_channel_modes[channel] = mode;

	return OK;
}

/**
 * Set PWM rate (frequency) for a timer
 */
int io_timer_set_rate(unsigned timer, unsigned rate)
{
	if (timer >= MAX_IO_TIMERS) {
		return -EINVAL;
	}

	if (rate < 50 || rate > 8000) {
		return -EINVAL;
	}

	/* Calculate new period */
	uint32_t period = TC_CLOCK_FREQ / rate;
	g_timer_period[timer] = period;

	/* Update RC register for all channels using this timer */
	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if (timer_io_channels[ch].timer_index == timer &&
		    g_channel_modes[ch] == IOTimerChanMode_PWMOut) {
			uint32_t base = get_channel_base(ch);
			tc_putreg(base, TC_RC_OFFSET, period);
		}
	}

	return OK;
}

/**
 * Enable/disable PWM channels
 */
int io_timer_set_enable(bool state, io_timer_channel_mode_t mode,
			io_timer_channel_allocation_t masks)
{
	for (unsigned ch = 0; ch < MAX_TIMER_IO_CHANNELS; ch++) {
		if ((masks & (1 << ch)) && g_channel_modes[ch] == mode) {
			uint32_t base = get_channel_base(ch);

			if (state) {
				tc_putreg(base, TC_CCR_OFFSET, TC_CCR_CLKEN | TC_CCR_SWTRG);

			} else {
				tc_putreg(base, TC_CCR_OFFSET, TC_CCR_CLKDIS);
			}
		}
	}

	return OK;
}

/**
 * Set PWM duty cycle (CCR = Compare Capture Register, here RA)
 * Value is in microseconds (1000-2000 typical for servos)
 */
int io_timer_set_ccr(unsigned channel, uint16_t value)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return -EINVAL;
	}

	if (g_channel_modes[channel] != IOTimerChanMode_PWMOut) {
		return -EINVAL;
	}

	uint32_t base = get_channel_base(channel);

	/* Convert microseconds to timer ticks */
	/* TC_CLOCK_FREQ ticks per second, value is in microseconds */
	uint32_t ticks = (uint32_t)value * TC_CLOCK_FREQ / 1000000UL;

	/* Clamp to period */
	uint8_t timer_idx = timer_io_channels[channel].timer_index;

	if (ticks > g_timer_period[timer_idx]) {
		ticks = g_timer_period[timer_idx];
	}

	tc_putreg(base, TC_RA_OFFSET, ticks);

	return OK;
}

/**
 * Get current CCR value
 */
uint16_t io_channel_get_ccr(unsigned channel)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return 0;
	}

	uint32_t base = get_channel_base(channel);
	uint32_t ticks = tc_getreg(base, TC_RA_OFFSET);

	/* Convert back to microseconds */
	return (uint16_t)(ticks * 1000000UL / TC_CLOCK_FREQ);
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

	uint32_t base = get_channel_base(channel);

	/* Disable the channel */
	tc_putreg(base, TC_CCR_OFFSET, TC_CCR_CLKDIS);

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
 * Get GPIO configuration for PWM input (not used for SAMV7 PWM output)
 */
uint32_t io_timer_channel_get_as_pwm_input(unsigned channel)
{
	(void)channel;
	return 0;  /* PWM input not implemented */
}

/**
 * Unallocate a channel (alias for io_timer_free_channel)
 */
int io_timer_unallocate_channel(unsigned channel)
{
	return io_timer_free_channel(channel);
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
 * For TC, each channel runs independently, so this is a no-op
 */
void io_timer_trigger(unsigned channels_mask)
{
	/* TC channels run continuously in waveform mode.
	 * Unlike STM32 where you might need to trigger an update,
	 * SAMV7 TC channels update automatically on period boundary.
	 */
	(void)channels_mask;
}