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

#pragma once

/**
 * @file io_timer_hw_description.h
 *
 * SAMV7 Timer/Counter and PWMC hardware description for PWM output.
 * Provides constexpr helpers for timer_config.cpp board configuration.
 */

#include <px4_arch/io_timer.h>
#include <px4_arch/hw_description.h>
#include <px4_platform_common/constexpr_util.h>

/* NuttX hardware headers for PMC register addresses */
#include "hardware/sam_pmc.h"

#define initIOTimerChannelCapture initIOTimerChannel  /* alias for param metadata generation */

/* PWMC channel register layout constants (from DS60001527J Section 38.7) */
#define PWMC_CDTY_CH_OFFSET    0x04    /* CDTY offset from channel register group base */

/*******************************************************************************
 * TC (Timer/Counter) Helper Functions -- DEPRECATED
 *
 * TC-based PWM replaced by PWMC (io_timer_pwmc.c).
 * Retained for reference and potential TC capture use (Phase 3).
 ******************************************************************************/

/**
 * Initialize an IO timer block (TC-based) -- DEPRECATED
 */
static inline constexpr io_timers_t initIOTimer(Timer::Timer timer)
{
	io_timers_t ret{};

	switch (timer) {
	case Timer::Timer1:
	case Timer::Timer2:
		ret.base = SAM_TC012_BASE;
		break;

	case Timer::Timer3:
	case Timer::Timer4:
	case Timer::Timer5:
		ret.base = SAM_TC345_BASE;
		break;

	case Timer::Timer6:
		ret.base = SAM_TC678_BASE;
		break;
	}

	ret.clock_register = 0;
	ret.clock_bit = 0;
	ret.clock_freq = 0;
	ret.vectorno = 0;
	ret.dshot = {};

	return ret;
}

/**
 * Initialize a timer channel with GPIO pin mapping (TC-based) -- DEPRECATED
 */
static inline constexpr timer_io_channels_t initIOTimerChannel(const io_timers_t io_timers_conf[MAX_IO_TIMERS],
		Timer::TimerChannel timer, GPIO::GPIOPin pin)
{
	timer_io_channels_t ret{};

	uint32_t gpio_mode = (4 << 21);  /* GPIO_PERIPHB */
	uint32_t gpio_cfg = (0 << 16);   /* GPIO_CFG_DEFAULT */
	uint32_t gpio_port = ((uint32_t)pin.port << 5);
	uint32_t gpio_pin = (uint32_t)pin.pin;

	ret.gpio_out = gpio_mode | gpio_cfg | gpio_port | gpio_pin;
	ret.gpio_in = 0;

	ret.timer_index = 0xff;

	switch (timer.timer) {
	case Timer::Timer1: ret.timer_index = 0; ret.timer_channel = 1; break;
	case Timer::Timer2: ret.timer_index = 1; ret.timer_channel = 2; break;
	case Timer::Timer3: ret.timer_index = 2; ret.timer_channel = 0; break;
	case Timer::Timer4: ret.timer_index = 3; ret.timer_channel = 1; break;
	case Timer::Timer5: ret.timer_index = 4; ret.timer_channel = 2; break;
	case Timer::Timer6: ret.timer_index = 5; ret.timer_channel = 0; break;
	}

	ret.masks = 0;
	ret.ccr_offset = 0;

	constexpr_assert(ret.timer_index != 0xff, "Timer not found");

	return ret;
}

/*******************************************************************************
 * PWMC (PWM Controller) Helper Functions
 *
 * SAMV7 PWMC provides hardware PWM with 4 channels per module.
 * - PWM0: PID 31, base 0x40020000, XDMAC TX ch 13
 * - PWM1: PID 60, base 0x4005C000, XDMAC TX ch 39
 ******************************************************************************/

/**
 * Initialize an IO timer for PWMC (PWM Controller)
 * @param module PWM0 or PWM1
 */
static inline constexpr io_timers_t initIOPWMTimer(PWM::PWMModule module)
{
	io_timers_t ret{};

	ret.base = pwmBaseRegister(module);
	ret.clock_freq = 150000000UL;  /* MCK = 150MHz */
	ret.dshot = {};

	switch (module) {
	case PWM::PWM0:
		ret.clock_register = SAM_PMC_PCER0;
		ret.clock_bit = (1u << SAM_PID_PWM0);         /* PID 31 in PCER0 */
		ret.vectorno = SAM_IRQ_PWM0;
		ret.dshot.xdmac_ch_tx = SAM_PID_PWM0;        /* NuttX PID for sam_dmachannel() lookup */
		break;

	case PWM::PWM1:
		ret.clock_register = SAM_PMC_PCER1;
		ret.clock_bit = (1u << (SAM_PID_PWM1 - 32));  /* PID 60 in PCER1 (bit 28) */
		ret.vectorno = SAM_IRQ_PWM1;
		ret.dshot.xdmac_ch_tx = SAM_PID_PWM1;        /* NuttX PID for sam_dmachannel() lookup */
		break;
	}

	return ret;
}

/**
 * Initialize a PWMC channel with GPIO pin mapping
 * @param io_timers_conf Array of io_timers_t (for future validation)
 * @param pwm_channel PWM module and channel (e.g., {PWM0, Channel0})
 * @param pin GPIO port and pin (e.g., {PortB, Pin0})
 * @param periph Peripheral function A or B (pin-dependent, see samv71_pinmap.h)
 */
static inline constexpr timer_io_channels_t initIOPWMChannel(const io_timers_t io_timers_conf[MAX_IO_TIMERS],
		PWM::PWMChannel pwm_channel, GPIO::GPIOPin pin, PWMCPeripheral periph)
{
	timer_io_channels_t ret{};

	/* GPIO configuration: Peripheral A (3) or B (4) depending on pin
	 * NuttX GPIO encoding for SAMV7:
	 * - Bits 21-23: Mode (GPIO_PERIPHA = 3 << 21, GPIO_PERIPHB = 4 << 21)
	 * - Bits 16-20: Config (GPIO_CFG_DEFAULT = 0)
	 * - Bits 5-7:   Port (PIOA=0, PIOB=1, PIOC=2, PIOD=3, PIOE=4)
	 * - Bits 0-4:   Pin number (0-31)
	 */
	uint32_t gpio_mode = (periph == PWMCPeripheral::A) ? (3 << 21) : (4 << 21);
	uint32_t gpio_cfg = (0 << 16);  /* GPIO_CFG_DEFAULT */
	uint32_t gpio_port = ((uint32_t)pin.port << 5);
	uint32_t gpio_pin = (uint32_t)pin.pin;

	ret.gpio_out = gpio_mode | gpio_cfg | gpio_port | gpio_pin;
	ret.gpio_in = 0;

	/* Derive timer_index from PWM module: PWM0 = index 0, PWM1 = index 1 */
	ret.timer_index = (uint8_t)pwm_channel.module;

	/* Channel within PWMC (0-3) for register offset calculation */
	ret.timer_channel = (uint8_t)pwm_channel.channel;

	/* Channel bit in PWMC SR/ISR registers */
	ret.masks = (uint16_t)(1u << (uint8_t)pwm_channel.channel);

	/* CDTY offset from channel register group base */
	ret.ccr_offset = PWMC_CDTY_CH_OFFSET;

	return ret;
}

/* Initialize a TC channel as an io_timers_t entry.
 * base = per-channel address (timerBaseRegister already adds channel offset).
 * clock_freq = MCK/8 = 18.75 MHz (TC waveform clock source).
 */
static inline constexpr io_timers_t initIOTCTimer(Timer::Timer timer)
{
	io_timers_t ret{};
	ret.base           = timerBaseRegister(timer);
	ret.clock_freq     = 150000000UL / 8;   /* MCK/8 = 18.75 MHz */
	ret.clock_register = SAM_PMC_PCER0;

	switch (timer) {
	case Timer::Timer1: ret.clock_bit = (1u << SAM_PID_TC1); ret.vectorno = SAM_IRQ_TC1; break;
	case Timer::Timer2: ret.clock_bit = (1u << SAM_PID_TC2); ret.vectorno = SAM_IRQ_TC2; break;
	case Timer::Timer3: ret.clock_bit = (1u << SAM_PID_TC3); ret.vectorno = SAM_IRQ_TC3; break;
	case Timer::Timer4: ret.clock_bit = (1u << SAM_PID_TC4); ret.vectorno = SAM_IRQ_TC4; break;
	case Timer::Timer5: ret.clock_bit = (1u << SAM_PID_TC5); ret.vectorno = SAM_IRQ_TC5; break;
	case Timer::Timer6:
		ret.clock_register = SAM_PMC_PCER1;
		ret.clock_bit = (1u << (SAM_PID_TC6 - 32));  /* PID 47 → PCER1 bit 15 */
		ret.vectorno = SAM_IRQ_TC6;
		break;
	default: break;
	}

	return ret;
}

/* TIOA channel — duty via RA register (ccr_offset=0x14), Peripheral B */
static inline constexpr timer_io_channels_t initIOTCChannelTIOA(
	const io_timers_t io_timers_conf[MAX_IO_TIMERS],
	unsigned timer_index, GPIO::GPIOPin pin)
{
	timer_io_channels_t ret{};
	ret.gpio_out    = (4u << 21) | (0u << 16) | ((uint32_t)pin.port << 5) | (uint32_t)pin.pin;
	ret.gpio_in     = 0;
	ret.timer_index = (uint8_t)timer_index;
	ret.timer_channel = 0;
	ret.masks       = 0;
	ret.ccr_offset  = 0x14;   /* TC_RA_OFFSET */
	ret.is_tc       = 1;
	return ret;
}

/* TIOB channel — duty via RB register (ccr_offset=0x18), Peripheral B */
static inline constexpr timer_io_channels_t initIOTCChannelTIOB(
	const io_timers_t io_timers_conf[MAX_IO_TIMERS],
	unsigned timer_index, GPIO::GPIOPin pin)
{
	timer_io_channels_t ret{};
	ret.gpio_out    = (4u << 21) | (0u << 16) | ((uint32_t)pin.port << 5) | (uint32_t)pin.pin;
	ret.gpio_in     = 0;
	ret.timer_index = (uint8_t)timer_index;
	ret.timer_channel = 0;
	ret.masks       = 0;
	ret.ccr_offset  = 0x18;   /* TC_RB_OFFSET */
	ret.is_tc       = 1;
	return ret;
}

/* Use common initIOTimerChannelMapping from px4_platform */
#include <px4_platform/io_timer_init.h>