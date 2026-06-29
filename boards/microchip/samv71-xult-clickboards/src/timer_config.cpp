/****************************************************************************
 *
 *   Copyright (C) 2024-2025 PX4 Development Team. All rights reserved.
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
 * @file timer_config.cpp
 *
 * PWMC-only PWM configuration for SAMV71 Custom FC — 4 channels.
 *
 * Motor 1 (CH0): PB0  - GPIO_PWMC0_H0 (Peripheral A)
 * Motor 2 (CH1): PA2  - GPIO_PWMC0_H1 (Peripheral A)
 * Motor 3 (CH2): PC19 - GPIO_PWMC0_H2 (Peripheral B)
 * Motor 4 (CH3): PC13 - GPIO_PWMC0_H3 (Peripheral B)
 *
 * Clock: MCK/8 = 18.75 MHz, 400 Hz → CPRD = 46875
 */

#include <px4_arch/io_timer_hw_description.h>

const io_timers_t io_timers[MAX_IO_TIMERS] = {
	initIOPWMTimer(PWM::PWM0),
};

const timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS] = {
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel0}, {GPIO::PortB, GPIO::Pin0},  PWMCPeripheral::A),  /* Motor 1 - PB0  */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel1}, {GPIO::PortA, GPIO::Pin2},  PWMCPeripheral::A),  /* Motor 2 - PA2  */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel2}, {GPIO::PortC, GPIO::Pin19}, PWMCPeripheral::B),  /* Motor 3 - PC19 */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel3}, {GPIO::PortC, GPIO::Pin13}, PWMCPeripheral::B),  /* Motor 4 - PC13 */
};

const io_timers_channel_mapping_t io_timers_channel_mapping =
	initIOTimerChannelMapping(io_timers, timer_io_channels);
