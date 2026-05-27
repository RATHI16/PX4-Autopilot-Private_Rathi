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
 * SAMV7 DShot — XDMAC for PWMC (ch0-3), ISR for TC (ch4-7).
 *
 * PWMC channels use Synchronous Channel Mode (SCM UPDM=2) + XDMAC.
 * The DMA writes duty values to PWM_DMAR; hardware distributes to
 * each synchronized channel's CDTYUPD at each period boundary.
 * One DMA transfer outputs the entire 16-bit frame with zero ISR overhead.
 *
 * TC channels retain per-channel CPCS ISR writing RA (16 bits exact).
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <nuttx/irq.h>
#include <nuttx/cache.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <drivers/drv_dshot.h>
#include <drivers/drv_hrt.h>
#include <px4_arch/io_timer.h>

#include "arm_internal.h"
#include "hardware/sam_pwm.h"
#include "hardware/sam_tc.h"
#include "sam_xdmac.h"
#include <arch/chip/irq.h>

/* PWM_DMAR register offset */
#define PWM_DMAR_OFF             0x024u

/* TC offsets */
#define TC_RA_OFF                SAM_TC_RA_OFFSET
#define TC_RC_OFF                SAM_TC_RC_OFFSET
#define TC_SR_OFF                SAM_TC_SR_OFFSET
#define TC_IER_OFF               SAM_TC_IER_OFFSET
#define TC_IDR_OFF               SAM_TC_IDR_OFFSET

#define DSHOT_MCK_HZ             150000000UL
#define DSHOT_PWMC_CLOCK_HZ      (DSHOT_MCK_HZ / 2u)
#define DSHOT_TC_CLOCK_HZ        (DSHOT_MCK_HZ / 8u)

#define DSHOT_FRAME_BITS         16u
#define DSHOT_TOTAL_PERIODS      18u      /* 16 data + 2 idle (UPDM=2 pipeline delay) */
#define DSHOT_HW_CHANNELS        4u
#define DSHOT_DMA_WORDS          (DSHOT_TOTAL_PERIODS * DSHOT_HW_CHANNELS)
#define DSHOT_DMA_BYTES          (DSHOT_DMA_WORDS * sizeof(uint32_t))
#define DSHOT_MAX_TC             4u

#define DSHOT_NO_MOTOR           (-1)

/* ─── PWMC DMA State ─── */
static uint32_t g_dma_buffer[DSHOT_DMA_WORDS] __attribute__((aligned(32)));
static DMA_HANDLE g_dma_handle;
static uint32_t g_dma_txflags;
static uint32_t g_dmar_addr;
static int8_t g_pwmc_hw_to_output[DSHOT_HW_CHANNELS];
static uint32_t g_pwmc_duty_0;
static uint32_t g_pwmc_duty_1;
static volatile bool g_pwmc_active;

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
static hrt_abstime g_last_trigger_time;

/* ─── Helpers ─── */

static uint16_t dshot_encode(uint16_t throttle, bool telemetry)
{
	uint16_t packet = (uint16_t)(((throttle & 0x07ffu) << 1) | (telemetry ? 1u : 0u));
	uint16_t crc = (uint16_t)((packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0fu);
	return (uint16_t)((packet << 4) | crc);
}

static void dshot_build_buffers(void)
{
	/* PWMC DMA buffer: 17 periods × 4 channels
	 * Layout: [period0_ch0, period0_ch1, period0_ch2, period0_ch3, period1_ch0, ...]
	 * Last 4 entries are 0 (idle/reset period).
	 */
	for (uint8_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
		uint16_t mask = (uint16_t)(1u << (DSHOT_FRAME_BITS - 1u - bit));

		for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
			int8_t output = g_pwmc_hw_to_output[hw_ch];
			uint32_t duty = 0;

			if (output >= 0 && (g_enabled_mask & (1u << output))) {
				duty = (g_packet[output] & mask) ? g_pwmc_duty_1 : g_pwmc_duty_0;
			}

			g_dma_buffer[(bit * DSHOT_HW_CHANNELS) + hw_ch] = duty;
		}
	}

	/* Two idle periods: UPDM=2 has 1-period pipeline latency,
	 * so first idle absorbs the stale bit-0 latch, second ensures LOW output.
	 */
	for (uint8_t p = 0; p < 2; p++) {
		for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
			g_dma_buffer[((DSHOT_FRAME_BITS + p) * DSHOT_HW_CHANNELS) + hw_ch] = 0;
		}
	}

	/* TC buffers — inverted polarity: RA = RC - duty */
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

/* ─── DMA Completion Callback ─── */

static void dshot_dma_callback(DMA_HANDLE handle, void *arg, int result)
{
	(void)handle;
	(void)arg;
	(void)result;
	g_pwmc_active = false;
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

	(void)getreg32(tc->base + TC_SR_OFF);

	uint8_t idx = tc->isr_idx;

	if (idx >= DSHOT_FRAME_BITS) {
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
	g_dma_handle = NULL;
	memset(g_dma_buffer, 0, sizeof(g_dma_buffer));
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
		g_dmar_addr = io_timers[timer].base + PWM_DMAR_OFF;
		io_timer_set_dshot_channel_mask(timer, io_timer_get_group(timer) & 0x0F);
	}

	/* Setup PWMC DShot mode (configures SCM UPDM=2 for sync+DMA) */
	io_timer_set_dshot_channel_mask(0, 0x0F);
	io_timer_set_dshot_mode(0, dshot_pwm_freq);

	/* ─── Allocate XDMAC channel for PWM0 TX ─── */
	g_dma_handle = sam_dmachannel(0, 0);

	if (g_dma_handle == NULL) {
		PX4_ERR("dshot: XDMAC channel allocation failed");
		return -ENOMEM;
	}

	g_dma_txflags = DMACH_FLAG_PERIPHPID(SAM_PID_PWM0) |
			DMACH_FLAG_PERIPHISPERIPH |
			DMACH_FLAG_PERIPHWIDTH_32BITS |
			DMACH_FLAG_PERIPHCHUNKSIZE_1 |
			DMACH_FLAG_PERIPHAHB_AHB_IF1 |
			DMACH_FLAG_MEMINCREMENT |
			DMACH_FLAG_MEMBURST_1;

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

		putreg32(tc_rc, tc->base + TC_RC_OFF);
		putreg32(0, tc->ra_addr);

		irq_attach(io_timers[timer_idx].vectorno, dshot_tc_isr, (void *)(uintptr_t)g_tc_count);
		up_enable_irq(io_timers[timer_idx].vectorno);

		g_enabled_mask |= (1u << output);
		g_packet[output] = dshot_encode(0, false);
		g_tc_count++;
	}

	dshot_build_buffers();
	g_initialized = true;

	PX4_INFO("dshot: init XDMAC+TC mask=0x%02" PRIx32 " tc_count=%u freq=%u",
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

	/* Check if PWMC DMA still active from previous frame */
	if (g_pwmc_active) {
		return;
	}

	/* Enforce minimum 500μs between frames to prevent double-triggers */
	hrt_abstime now = hrt_absolute_time();

	if ((now - g_last_trigger_time) < 500u) {
		return;
	}

	g_last_trigger_time = now;

	/* Check if any TC ISR still active */
	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (g_tc[t].active) {
			return;
		}
	}

	dshot_build_buffers();

	/* ─── PWMC: Start XDMAC transfer ─── */
	up_clean_dcache((uintptr_t)g_dma_buffer,
			(uintptr_t)g_dma_buffer + DSHOT_DMA_BYTES);

	sam_dmastop(g_dma_handle);
	sam_dmaconfig(g_dma_handle, g_dma_txflags);

	int ret = sam_dmatxsetup(g_dma_handle, g_dmar_addr,
				 (uint32_t)(uintptr_t)g_dma_buffer, DSHOT_DMA_BYTES);

	if (ret == OK) {
		g_pwmc_active = true;
		sam_dmastart(g_dma_handle, dshot_dma_callback, NULL);
	}

	/* ─── TC: pre-write bit 0 and enable ISR ─── */
	for (uint8_t t = 0; t < g_tc_count; t++) {
		if (!g_tc[t].enabled) {
			continue;
		}

		putreg32(g_tc[t].buffer[0], g_tc[t].ra_addr);
		g_tc[t].isr_idx = 1;
		g_tc[t].active = true;
		(void)getreg32(g_tc[t].base + TC_SR_OFF);
	}

	/* Enable TC interrupts */
	irqstate_t flags = enter_critical_section();

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

	/* Disarm: stop DMA and TC ISRs */
	if (g_dma_handle) {
		sam_dmastop(g_dma_handle);
	}

	g_pwmc_active = false;

	for (uint8_t t = 0; t < g_tc_count; t++) {
		putreg32(TC_INT_CPCS, g_tc[t].base + TC_IDR_OFF);
		putreg32(0, g_tc[t].ra_addr);
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
	PX4_INFO("dshot: PWMC(4ch XDMAC) + TC(%uch ISR)", g_tc_count);
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
