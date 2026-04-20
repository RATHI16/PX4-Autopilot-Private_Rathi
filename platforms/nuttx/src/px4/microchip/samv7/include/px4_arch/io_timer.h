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
 * @file io_timer.h
 *
 * SAMV7 IO Timer API — converged with STM32 io_timer.h signatures.
 * Uses PWMC (PWM Controller) for motor output instead of TC waveform mode.
 */

#pragma once

#include <px4_platform_common/px4_config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <drivers/drv_hrt.h>
#include <stdint.h>

#include "dshot.h"

__BEGIN_DECLS

/* Configuration limits — derived from board config when available */
#ifdef BOARD_NUM_IO_TIMERS
#define MAX_IO_TIMERS		BOARD_NUM_IO_TIMERS
#else
#define MAX_IO_TIMERS		2
#endif

#if defined(DIRECT_PWM_OUTPUT_CHANNELS) && DIRECT_PWM_OUTPUT_CHANNELS > 8
#define MAX_TIMER_IO_CHANNELS	DIRECT_PWM_OUTPUT_CHANNELS
#else
#define MAX_TIMER_IO_CHANNELS	8
#endif

#define MAX_LED_TIMERS		2
#define MAX_TIMER_LED_CHANNELS	6

#define IO_TIMER_ALL_MODES_CHANNELS 0

/* Channel mode enum — matches STM32 io_timer.h */
typedef enum io_timer_channel_mode_t {
	IOTimerChanMode_NotUsed         = 0,
	IOTimerChanMode_PWMOut          = 1,
	IOTimerChanMode_PWMIn           = 2,
	IOTimerChanMode_Capture         = 3,
	IOTimerChanMode_OneShot         = 4,
	IOTimerChanMode_Trigger         = 5,
	IOTimerChanMode_Dshot           = 6,
	IOTimerChanMode_LED             = 7,
	IOTimerChanMode_PPS             = 8,
	IOTimerChanMode_RPM             = 9,
	IOTimerChanMode_Other           = 10,
	IOTimerChanMode_DshotInverted   = 11,
	IOTimerChanMode_CaptureDMA      = 12,
	IOTimerChanModeSize
} io_timer_channel_mode_t;

typedef uint16_t io_timer_channel_allocation_t; /* big enough for MAX_TIMER_IO_CHANNELS */

/* Timer (PWMC module) descriptor — matches STM32 io_timers_t layout */
typedef struct io_timers_t {
	uint32_t     base;            /* PWMC base address (e.g., 0x40020000 for PWM0) */
	uint32_t     clock_register;  /* PMC clock enable register */
	uint32_t     clock_bit;       /* PMC clock enable bit */
	uint32_t     clock_freq;      /* Input clock frequency (MCK, typically 150MHz) */
	uint32_t     vectorno;        /* IRQ vector number */
	dshot_conf_t dshot;           /* DShot XDMAC configuration */
} io_timers_t;

/* Channel-to-timer mapping element — matches STM32 */
typedef struct io_timers_channel_mapping_element_t {
	uint32_t first_channel_index;
	uint32_t channel_count;
	uint32_t lowest_timer_channel;
	uint32_t channel_count_including_gaps;
} io_timers_channel_mapping_element_t;

typedef struct io_timers_channel_mapping_t {
	io_timers_channel_mapping_element_t element[MAX_IO_TIMERS];
} io_timers_channel_mapping_t;

/* Channel descriptor — matches STM32 timer_io_channels_t layout */
typedef struct timer_io_channels_t {
	uint32_t  gpio_out;       /* GPIO config for peripheral output */
	uint32_t  gpio_in;        /* GPIO config for input (capture) */
	uint8_t   timer_index;    /* Index into io_timers[] array */
	uint8_t   timer_channel;  /* PWMC channel number (0-3) */
	uint16_t  masks;          /* Channel bit in SR/ISR registers (1 << timer_channel) */
	uint8_t   ccr_offset;     /* Offset to CDTY register from channel base */
  uint8_t   is_tc;          /* 0 = PWMC, 1 = TC */
} timer_io_channels_t;

/* Channel handler callback — matches STM32 signature */
typedef void (*channel_handler_t)(void *context, const io_timers_t *timer,
				  uint32_t chan_index, const timer_io_channels_t *chan,
				  hrt_abstime isrs_time, uint16_t isrs_rcnt);

/* Board-supplied data */
__EXPORT extern const io_timers_t io_timers[MAX_IO_TIMERS];
__EXPORT extern const io_timers_channel_mapping_t io_timers_channel_mapping;
__EXPORT extern const timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS];

__EXPORT extern const io_timers_t led_pwm_timers[MAX_LED_TIMERS];
__EXPORT extern const timer_io_channels_t led_pwm_channels[MAX_TIMER_LED_CHANNELS];

/* Timer allocation */
__EXPORT int io_timer_allocate_timer(unsigned timer, io_timer_channel_mode_t mode);
__EXPORT int io_timer_unallocate_timer(unsigned timer);

/* Channel allocation */
__EXPORT int io_timer_allocate_channel(unsigned channel, io_timer_channel_mode_t mode);
__EXPORT int io_timer_unallocate_channel(unsigned channel);

/* Initialization */
__EXPORT int io_timer_init_timer(unsigned timer, io_timer_channel_mode_t mode);
__EXPORT int io_timer_channel_init(unsigned channel, io_timer_channel_mode_t mode,
				   channel_handler_t channel_handler, void *context);

/* Rate and duty cycle */
__EXPORT int io_timer_set_pwm_rate(unsigned timer, unsigned rate);
__EXPORT int io_timer_set_enable(bool state, io_timer_channel_mode_t mode,
				 io_timer_channel_allocation_t masks);
__EXPORT int io_timer_set_ccr(unsigned channel, uint16_t value);
__EXPORT uint16_t io_channel_get_ccr(unsigned channel);

/* Channel queries */
__EXPORT uint32_t io_timer_get_group(unsigned timer);
__EXPORT int io_timer_validate_channel_index(unsigned channel);
__EXPORT int io_timer_is_channel_free(unsigned channel);
__EXPORT int io_timer_free_channel(unsigned channel);
__EXPORT int io_timer_get_channel_mode(unsigned channel);
__EXPORT int io_timer_get_mode_channels(io_timer_channel_mode_t mode);

/* GPIO helpers */
__EXPORT uint32_t io_timer_channel_get_gpio_output(unsigned channel);
__EXPORT uint32_t io_timer_channel_get_as_pwm_input(unsigned channel);

/* OneShot trigger */
__EXPORT void io_timer_trigger(unsigned channels_mask);

/* DShot support */
__EXPORT void io_timer_update_dma_req(uint8_t timer, bool enable);
__EXPORT int io_timer_set_dshot_mode(uint8_t timer, unsigned dshot_pwm_freq);

__END_DECLS
