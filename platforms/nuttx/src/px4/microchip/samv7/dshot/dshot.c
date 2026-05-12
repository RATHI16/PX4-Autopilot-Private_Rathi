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
 * @file dshot.c
 *
 * SAMV7 DShot — ISR-driven, 8 channels (4 PWMC + 4 TC).
 *
 * PWMC ch0-3: PWM0 CH0 period ISR writes CDTYUPD (16 bits exact)
 * TC ch4-7: Each TC has its own CPCS ISR writing RA (16 bits exact)
 *
 * Each peripheral uses its OWN interrupt synchronized to its OWN
 * period counter — eliminates cross-peripheral race conditions.
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <nuttx/irq.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <drivers/drv_dshot.h>
#include <px4_arch/io_timer.h>

#include "arm_internal.h"
#include "hardware/sam_pwm.h"
#include "hardware/sam_tc.h"

/* PWMC offsets */
#define PWM_CH_CDTYUPD(base, ch) ((base) + 0x200u + ((ch) * 0x20u) + 0x08u)
#define PWM_IER1_OFF             0x10u
#define PWM_IDR1_OFF             0x14u
#define PWM_ISR1_OFF             0x1Cu

/* TC offsets */
#define TC_RA_OFF                SAM_TC_RA_OFFSET
#define TC_RC_OFF                SAM_TC_RC_OFFSET
#define TC_SR_OFF                SAM_TC_SR_OFFSET
#define TC_IER_OFF               SAM_TC_IER_OFFSET
#define TC_IDR_OFF               SAM_TC_IDR_OFFSET
/* TC_INT_CPCS already defined in hardware/sam_tc.h */

#define DSHOT_MCK_HZ             150000000UL
#define DSHOT_PWMC_CLOCK_HZ      (DSHOT_MCK_HZ / 2u)
#define DSHOT_TC_CLOCK_HZ        (DSHOT_MCK_HZ / 8u)

#define DSHOT_FRAME_BITS         16u
#define DSHOT_HW_CHANNELS        4u
#define DSHOT_BUFFER_WORDS       (DSHOT_FRAME_BITS * DSHOT_HW_CHANNELS)
#define DSHOT_MAX_TC             4u

#define DSHOT_NO_MOTOR           (-1)

/* ─── PWMC State ─── */
static uint32_t g_pwmc_buffer[DSHOT_BUFFER_WORDS];
static int8_t g_pwmc_hw_to_output[DSHOT_HW_CHANNELS];
static uint32_t g_pwmc_duty_0;
static uint32_t g_pwmc_duty_1;
static volatile uint8_t g_pwmc_isr_idx;
static volatile bool g_pwmc_active;
static uint32_t g_pwm_base;

/* ─── TC State (one per TC channel) ─── */
struct tc_dshot_state {
	uint32_t buffer[DSHOT_FRAME_BITS];
	uint32_t base;
	uint32_t ra_addr;
	uint32_t rc_value;
	uint8_t output_idx;
	volatile uint8_t isr_idx;
	volatile bool active;
	bool enabled;
};

static struct tc_dshot_state g_tc[DSHOT_MAX_TC];
static uint8_t g_tc_count;
static uint32_t g_tc_duty_0;
static uint32_t g_tc_duty_1;

/* ─── Common State ─── */
static uint16_t g_packet[MAX_TIMER_IO_CHANNELS];
static uint32_t g_enabled_mask;
static bool g_initialized;
static bool g_armed;

/* ─── Helpers ─── */

static uint16_t dshot_encode(uint16_t throttle, bool telemetry)
{
	uint16_t packet = (uint16_t)(((throttle & 0x07ffu) << 1) | (telemetry ? 1u : 0u));
	uint16_t crc = (uint16_t)((packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0fu);
	return (uint16_t)((packet << 4) | crc);
}

static void dshot_build_buffers(void)
{
	/* PWMC buffer */
	for (uint8_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
		uint16_t mask = (uint16_t)(1u << (DSHOT_FRAME_BITS - 1u - bit));

		for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
			int8_t output = g_pwmc_hw_to_output[hw_ch];
			uint32_t duty = 0;

			if (output >= 0 && (g_enabled_mask & (1u << output))) {
				duty = (g_packet[output] & mask) ? g_pwmc_duty_1 : g_pwmc_duty_0;
			}

			g_pwmc_buffer[(bit * DSHOT_HW_CHANNELS) + hw_ch] = duty;
		}
	}

	/* TC buffers — inverted polarity: RA = RC - duty
	 * (output is LOW from 0 to RA, then HIGH from RA to RC)
	 */
	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (!g_tc[t].enabled) {
			continue;
		}

		uint8_t output = g_tc[t].output_idx;
		uint32_t rc = g_tc[t].rc_value;

		for (uint8_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
			uint16_t mask = (uint16_t)(1u << (DSHOT_FRAME_BITS - 1u - bit));
			uint32_t duty = (g_packet[output] & mask) ? g_tc_duty_1 : g_tc_duty_0;
			g_tc[t].buffer[bit] = rc - duty;
		}
	}
}

/* ─── PWMC ISR (CH0 period-end) ─── */

static int dshot_pwmc_isr(int irq, void *context, void *arg)
{
	(void)irq; (void)context; (void)arg;

	uint32_t base = g_pwm_base;
	(void)getreg32(base + PWM_ISR1_OFF);

	uint8_t idx = g_pwmc_isr_idx;

	if (idx >= DSHOT_FRAME_BITS) {
		putreg32(0, PWM_CH_CDTYUPD(base, 0));
		putreg32(0, PWM_CH_CDTYUPD(base, 1));
		putreg32(0, PWM_CH_CDTYUPD(base, 2));
		putreg32(0, PWM_CH_CDTYUPD(base, 3));
		putreg32(0x01u, base + PWM_IDR1_OFF);
		g_pwmc_active = false;
		return OK;
	}

	uint32_t *src = &g_pwmc_buffer[idx * DSHOT_HW_CHANNELS];
	putreg32(src[0], PWM_CH_CDTYUPD(base, 0));
	putreg32(src[1], PWM_CH_CDTYUPD(base, 1));
	putreg32(src[2], PWM_CH_CDTYUPD(base, 2));
	putreg32(src[3], PWM_CH_CDTYUPD(base, 3));

	g_pwmc_isr_idx = idx + 1;
	return OK;
}

/* ─── TC ISR (one per TC channel, fires on CPCS = RC compare) ─── */

static int dshot_tc_isr(int irq, void *context, void *arg)
{
	(void)irq; (void)context;

	uint8_t tc_idx = (uint8_t)(uintptr_t)arg;

	if (tc_idx >= DSHOT_MAX_TC) {
		return OK;
	}

	struct tc_dshot_state *tc = &g_tc[tc_idx];

	/* Reading SR clears CPCS interrupt flag */
	(void)getreg32(tc->base + TC_SR_OFF);

	uint8_t idx = tc->isr_idx;

	if (idx >= DSHOT_FRAME_BITS) {
		/* Frame done: set RA = 0 (no SET event → output stays LOW) */
		putreg32(0, tc->ra_addr);
		putreg32(TC_INT_CPCS, tc->base + TC_IDR_OFF);
		tc->active = false;
		return OK;
	}

	putreg32(tc->buffer[idx], tc->ra_addr);
	tc->isr_idx = idx + 1;
	return OK;
}

/* ─── Public API ─── */

int up_dshot_init(uint32_t channel_mask, unsigned dshot_pwm_freq, bool enable_bidirectional)
{
	(void)enable_bidirectional;

	if (dshot_pwm_freq == 0) {
		dshot_pwm_freq = 300000;
	}

	g_initialized = false;
	g_armed = false;
	g_enabled_mask = 0;
	g_pwmc_active = false;
	g_tc_count = 0;
	memset(g_pwmc_buffer, 0, sizeof(g_pwmc_buffer));
	memset(g_packet, 0, sizeof(g_packet));
	memset(g_tc, 0, sizeof(g_tc));

	for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
		g_pwmc_hw_to_output[hw_ch] = DSHOT_NO_MOTOR;
	}

	/* PWMC timing (MCK/2 = 75MHz) */
	uint32_t pwmc_cprd = DSHOT_PWMC_CLOCK_HZ / dshot_pwm_freq;
	g_pwmc_duty_0 = (pwmc_cprd * 3u + 4u) / 8u;
	g_pwmc_duty_1 = (pwmc_cprd * 3u + 2u) / 4u;

	/* TC timing (MCK/8 = 18.75MHz) */
	uint32_t tc_rc = DSHOT_TC_CLOCK_HZ / dshot_pwm_freq;
	g_tc_duty_0 = (tc_rc * 3u + 4u) / 8u;
	g_tc_duty_1 = (tc_rc * 3u + 2u) / 4u;

	/* Initialize timers */
	for (uint8_t timer = 0; timer < MAX_IO_TIMERS; timer++) {
		io_timer_set_dshot_channel_mask(timer, 0);
	}

	/* ─── Configure PWMC channels (0-3) ─── */
	for (uint8_t output = 0; output < MAX_TIMER_IO_CHANNELS; output++) {
		if (!(channel_mask & (1u << output))) {
			continue;
		}

		if (timer_io_channels[output].is_tc) {
			continue;
		}

		uint8_t timer = timer_io_channels[output].timer_index;
		uint8_t hw_ch = timer_io_channels[output].timer_channel;

		if (hw_ch >= DSHOT_HW_CHANNELS) {
			continue;
		}

		int ret = io_timer_channel_init(output, IOTimerChanMode_Dshot, NULL, NULL);

		if (ret != OK && ret != -EBUSY) {
			continue;
		}

		g_pwmc_hw_to_output[hw_ch] = (int8_t)output;
		g_enabled_mask |= (1u << output);
		g_packet[output] = dshot_encode(0, false);
		g_pwm_base = io_timers[timer].base;
		io_timer_set_dshot_channel_mask(timer, io_timer_get_group(timer) & 0x0F);
	}

	/* Setup PWMC DShot mode */
	io_timer_set_dshot_channel_mask(0, 0x0F);
	io_timer_set_dshot_mode(0, dshot_pwm_freq);

	/* Attach PWMC ISR */
	irq_attach(io_timers[0].vectorno, dshot_pwmc_isr, NULL);
	up_enable_irq(io_timers[0].vectorno);

	/* ─── Configure TC channels (4-7) ─── */
	for (uint8_t output = 0; output < MAX_TIMER_IO_CHANNELS; output++) {
		if (!(channel_mask & (1u << output))) {
			continue;
		}

		if (!timer_io_channels[output].is_tc) {
			continue;
		}

		if (g_tc_count >= DSHOT_MAX_TC) {
			break;
		}

		uint8_t timer_idx = timer_io_channels[output].timer_index;

		int ret = io_timer_channel_init(output, IOTimerChanMode_Dshot, NULL, NULL);

		if (ret != OK && ret != -EBUSY) {
			continue;
		}

		struct tc_dshot_state *tc = &g_tc[g_tc_count];
		tc->base = io_timers[timer_idx].base;
		tc->ra_addr = tc->base + TC_RA_OFF;
		tc->rc_value = tc_rc;
		tc->output_idx = output;
		tc->enabled = true;

		/* Set RC for DShot period */
		putreg32(tc_rc, tc->base + TC_RC_OFF);

		/* Set RA = 0 (idle LOW — inverted: no SET event fires) */
		putreg32(0, tc->ra_addr);

		/* Attach TC ISR for this channel */
		irq_attach(io_timers[timer_idx].vectorno, dshot_tc_isr, (void *)(uintptr_t)g_tc_count);
		up_enable_irq(io_timers[timer_idx].vectorno);

		g_enabled_mask |= (1u << output);
		g_packet[output] = dshot_encode(0, false);
		g_tc_count++;
	}

	dshot_build_buffers();
	g_initialized = true;

	PX4_INFO("dshot: init pwmc+tc mask=0x%02" PRIx32 " tc_count=%u freq=%u",
		 g_enabled_mask, g_tc_count, dshot_pwm_freq);

	return (int)g_enabled_mask;
}

void dshot_motor_data_set(unsigned channel, uint16_t throttle, bool telemetry)
{
	if (channel >= MAX_TIMER_IO_CHANNELS) {
		return;
	}

	if (!(g_enabled_mask & (1u << channel))) {
		return;
	}

	g_packet[channel] = dshot_encode(throttle, telemetry);
}

void up_dshot_trigger(void)
{
	if (!g_initialized || !g_armed) {
		return;
	}

	/* Check if any ISR still active */
	if (g_pwmc_active) {
		return;
	}

	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (g_tc[t].active) {
			return;
		}
	}

	dshot_build_buffers();

	/* Pre-write bit 0 on BOTH PWMC and TC.
	 * ISRs start from index 1. Ensures exactly 16 pulses + sync start.
	 */

	/* PWMC: pre-write bit 0 to CDTYUPD (latches at next period boundary) */
	uint32_t *first = &g_pwmc_buffer[0];
	putreg32(first[0], PWM_CH_CDTYUPD(g_pwm_base, 0));
	putreg32(first[1], PWM_CH_CDTYUPD(g_pwm_base, 1));
	putreg32(first[2], PWM_CH_CDTYUPD(g_pwm_base, 2));
	putreg32(first[3], PWM_CH_CDTYUPD(g_pwm_base, 3));
	g_pwmc_isr_idx = 1;
	g_pwmc_active = true;

	/* TC: pre-write bit 0 to RA */
	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (!g_tc[t].enabled) {
			continue;
		}

		putreg32(g_tc[t].buffer[0], g_tc[t].ra_addr);
		g_tc[t].isr_idx = 1;
		g_tc[t].active = true;
		(void)getreg32(g_tc[t].base + TC_SR_OFF);
	}

	/* Clear PWMC ISR flag */
	(void)getreg32(g_pwm_base + PWM_ISR1_OFF);

	/* Enable ALL interrupts simultaneously */
	irqstate_t flags = enter_critical_section();

	putreg32(0x01u, g_pwm_base + PWM_IER1_OFF);

	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (g_tc[t].enabled) {
			putreg32(TC_INT_CPCS, g_tc[t].base + TC_IER_OFF);
		}
	}

	leave_critical_section(flags);
}

int up_dshot_arm(bool armed)
{
	g_armed = armed;

	if (!g_initialized) {
		return OK;
	}

	if (armed) {
		return io_timer_set_enable(true, IOTimerChanMode_Dshot, IO_TIMER_ALL_MODES_CHANNELS);
	}

	/* Disarm: stop all ISRs */
	putreg32(0x01u, g_pwm_base + PWM_IDR1_OFF);
	g_pwmc_active = false;

	for (uint8_t t = 0; t < g_tc_count; t++) {
		putreg32(TC_INT_CPCS, g_tc[t].base + TC_IDR_OFF);
		putreg32(0, g_tc[t].ra_addr);  /* RA=0 → idle LOW */
		g_tc[t].active = false;
	}

	io_timer_dshot_force_low(0);
	return io_timer_set_enable(false, IOTimerChanMode_Dshot, IO_TIMER_ALL_MODES_CHANNELS);
}

int up_bdshot_channel_status(uint8_t channel)
{
	(void)channel;
	return 0;
}

void up_bdshot_status(void)
{
	PX4_INFO("dshot: PWMC(4ch) + TC(%uch) ISR-driven", g_tc_count);
}

int up_bdshot_num_erpm_ready(void)
{
	return 0;
}

int up_bdshot_get_erpm(uint8_t channel, int *erpm)
{
	(void)channel;

	if (erpm != NULL) {
		*erpm = 0;
	}

	return -ENOSYS;
}
