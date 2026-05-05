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
 * SAMV7 DShot output — polled CDTYUPD approach (no XDMAC).
 *
 * Each call to up_dshot_trigger() bit-bangs one full DShot frame
 * (16 data bits + 1 reset period = 17 periods) by writing CDTYUPD
 * registers directly and waiting for each period boundary.
 *
 * At DShot300 (3.33µs per bit), a full frame takes ~57µs of CPU time.
 * This is acceptable at 1kHz PID loop rate (~5.7% utilization).
 *
 * Pin mapping (PWM0 CH0-CH3):
 *   output 0 -> PB0  -> PWM0 CH0
 *   output 1 -> PA2  -> PWM0 CH1
 *   output 2 -> PC19 -> PWM0 CH2
 *   output 3 -> PC13 -> PWM0 CH3
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

#define DSHOT_MCK_HZ              150000000UL
#define DSHOT_CLOCK_HZ            (DSHOT_MCK_HZ / 2u)

#define DSHOT_FRAME_BITS          16u
#define DSHOT_RESET_PERIODS       1u
#define DSHOT_TOTAL_PERIODS       (DSHOT_FRAME_BITS + DSHOT_RESET_PERIODS)
#define DSHOT_HW_CHANNELS         4u

#define DSHOT_NO_MOTOR            (-1)

/* PWMC per-channel register offsets */
#define PWM_CH_BASE(base, ch)     ((base) + 0x200u + ((ch) * 0x20u))
#define PWM_CDTYUPD_OFF           0x08u
#define PWM_ISR1_OFF              0x1Cu

static uint16_t g_packet[MAX_TIMER_IO_CHANNELS];

static int8_t g_hw_to_output[MAX_IO_TIMERS][DSHOT_HW_CHANNELS];
static uint32_t g_timer_hw_mask[MAX_IO_TIMERS];
static uint32_t g_enabled_mask;
static bool g_initialized;
static bool g_armed;

static uint32_t g_cprd[MAX_IO_TIMERS];
static uint32_t g_duty_0[MAX_IO_TIMERS];
static uint32_t g_duty_1[MAX_IO_TIMERS];

static uint16_t dshot_encode(uint16_t throttle, bool telemetry)
{
	uint16_t packet = (uint16_t)(((throttle & 0x07ffu) << 1) | (telemetry ? 1u : 0u));
	uint16_t crc = (uint16_t)((packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0fu);
	return (uint16_t)((packet << 4) | crc);
}

static void dshot_compute_timing(uint8_t timer, unsigned dshot_pwm_freq)
{
	g_cprd[timer] = DSHOT_CLOCK_HZ / dshot_pwm_freq;
	g_duty_0[timer] = (g_cprd[timer] * 3u + 4u) / 8u;
	g_duty_1[timer] = (g_cprd[timer] * 3u + 2u) / 4u;
}

int up_dshot_init(uint32_t channel_mask, unsigned dshot_pwm_freq, bool enable_bidirectional)
{
	(void)enable_bidirectional;

	if (dshot_pwm_freq == 0) {
		dshot_pwm_freq = 300000;
	}

	g_initialized = false;
	g_armed = false;
	g_enabled_mask = 0;
	memset(g_packet, 0, sizeof(g_packet));
	memset(g_timer_hw_mask, 0, sizeof(g_timer_hw_mask));

	for (uint8_t timer = 0; timer < MAX_IO_TIMERS; timer++) {
		io_timer_set_dshot_channel_mask(timer, 0);

		for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
			g_hw_to_output[timer][hw_ch] = DSHOT_NO_MOTOR;
		}
	}

	for (uint8_t output = 0; output < MAX_TIMER_IO_CHANNELS; output++) {
		if (!(channel_mask & (1u << output))) {
			continue;
		}

		if (timer_io_channels[output].is_tc) {
			continue;
		}

		uint8_t timer = timer_io_channels[output].timer_index;
		uint8_t hw_ch = timer_io_channels[output].timer_channel;

		if (timer >= MAX_IO_TIMERS || hw_ch >= DSHOT_HW_CHANNELS) {
			continue;
		}

		int ret = io_timer_channel_init(output, IOTimerChanMode_Dshot, NULL, NULL);

		if (ret != OK && ret != -EBUSY) {
			PX4_ERR("dshot: channel %u init failed: %d", output, ret);
			return ret;
		}

		g_hw_to_output[timer][hw_ch] = (int8_t)output;
		g_timer_hw_mask[timer] |= (1u << hw_ch);
		g_enabled_mask |= (1u << output);
		g_packet[output] = dshot_encode(0, false);
	}

	for (uint8_t timer = 0; timer < MAX_IO_TIMERS; timer++) {
		if (g_timer_hw_mask[timer] == 0) {
			continue;
		}

		dshot_compute_timing(timer, dshot_pwm_freq);
		io_timer_set_dshot_channel_mask(timer, g_timer_hw_mask[timer]);

		int ret = io_timer_set_dshot_mode(timer, dshot_pwm_freq);

		if (ret != OK) {
			PX4_ERR("dshot: timer %u mode failed: %d", timer, ret);
			return ret;
		}
	}

	g_initialized = true;

	PX4_INFO("dshot: init mask=0x%02" PRIx32 " freq=%u cprd0=%" PRIu32 " t0=%" PRIu32 " t1=%" PRIu32,
		 g_enabled_mask, dshot_pwm_freq, g_cprd[0], g_duty_0[0], g_duty_1[0]);

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

	for (uint8_t timer = 0; timer < MAX_IO_TIMERS; timer++) {
		if (g_timer_hw_mask[timer] == 0) {
			continue;
		}

		uint32_t base = io_timers[timer].base;
		uint32_t duty_0 = g_duty_0[timer];
		uint32_t duty_1 = g_duty_1[timer];

		irqstate_t flags = enter_critical_section();

		/* Sync to a clean period boundary before starting the frame */
		(void)getreg32(base + PWM_ISR1_OFF);

		while (!(getreg32(base + PWM_ISR1_OFF) & 0x01u)) {}

		/* Transmit 16 data bits + 1 reset = 17 periods.
		 * Write CDTYUPD then wait for boundary (value latches and bit transmits).
		 */
		for (uint8_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
			uint16_t bmask = (uint16_t)(1u << (DSHOT_FRAME_BITS - 1u - bit));

			for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
				int8_t output = g_hw_to_output[timer][hw_ch];
				uint32_t duty;

				if (output >= 0 && (g_enabled_mask & (1u << output))) {
					duty = (g_packet[output] & bmask) ? duty_1 : duty_0;
				} else {
					duty = 0;
				}

				putreg32(duty, PWM_CH_BASE(base, hw_ch) + PWM_CDTYUPD_OFF);
			}

			while (!(getreg32(base + PWM_ISR1_OFF) & 0x01u)) {}
		}

		/* Reset period: duty=0 (output LOW) */
		for (uint8_t hw_ch = 0; hw_ch < DSHOT_HW_CHANNELS; hw_ch++) {
			putreg32(0, PWM_CH_BASE(base, hw_ch) + PWM_CDTYUPD_OFF);
		}

		while (!(getreg32(base + PWM_ISR1_OFF) & 0x01u)) {}

		leave_critical_section(flags);
	}
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
	PX4_INFO("bdshot unsupported (polled dshot mode)");
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
