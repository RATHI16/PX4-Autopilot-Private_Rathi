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
 * Mixed PWMC + TC PWM configuration for SAMV71-XULT — 8 channels total.
 *
 * io_timers[0]: PWM0 (PWMC)      — ch1..4: PB0, PA2, PC19, PC13
 * io_timers[1]: TC0 CH1 (Timer1) — ch5: PA15 TIOA
 * io_timers[2]: TC1 CH0 (Timer3) — ch6: PC23 TIOA
 * io_timers[3]: TC1 CH2 (Timer5) — ch7: PC29 TIOA
 * io_timers[4]: TC2 CH0 (Timer6) — ch8: PC5  TIOA
 *
 * PWMC clock: MCK/8 = 18.75 MHz, 400 Hz → CPRD = 46875
 * TC clock:   MCK/8 = 18.75 MHz, 400 Hz → RC   = 46875
 *
 * NOTE: TC0 CH0 (PID 23) is used by HRT — PA0/PA1 are off-limits.
 *       PC29 (TC5) was reserved for RC Input; now used for ch7 PWM.
 */

#include <px4_arch/io_timer_hw_description.h>

const io_timers_t io_timers[MAX_IO_TIMERS] = {
	initIOPWMTimer(PWM::PWM0),       /* index 0: PWMC PWM0 — ch1..4 */
	initIOTCTimer(Timer::Timer1),    /* index 1: TC0 CH1 (PID 24) — PA15 */
	initIOTCTimer(Timer::Timer3),    /* index 2: TC1 CH0 (PID 26) — PC23 */
	initIOTCTimer(Timer::Timer5),    /* index 3: TC1 CH2 (PID 28) — PC29 */
	initIOTCTimer(Timer::Timer6),    /* index 4: TC2 CH0 (PID 47) — PC5  */
};

const timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS] = {
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel0}, {GPIO::PortB, GPIO::Pin0},  PWMCPeripheral::A),  /* ch1 - PB0  */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel1}, {GPIO::PortA, GPIO::Pin2},  PWMCPeripheral::A),  /* ch2 - PA2  */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel2}, {GPIO::PortC, GPIO::Pin19}, PWMCPeripheral::B),  /* ch3 - PC19 */
	initIOPWMChannel(io_timers, {PWM::PWM0, PWM::Channel3}, {GPIO::PortC, GPIO::Pin13}, PWMCPeripheral::B),  /* ch4 - PC13 */
	initIOTCChannelTIOA(io_timers, 1, {GPIO::PortA, GPIO::Pin15}),                                           /* ch5 - PA15 TIOA */
	initIOTCChannelTIOA(io_timers, 2, {GPIO::PortC, GPIO::Pin23}),                                           /* ch6 - PC23 TIOA */
	initIOTCChannelTIOA(io_timers, 3, {GPIO::PortC, GPIO::Pin29}),                                           /* ch7 - PC29 TIOA */
	initIOTCChannelTIOA(io_timers, 4, {GPIO::PortC, GPIO::Pin5}),                                            /* ch8 - PC5  TIOA */
};

const io_timers_channel_mapping_t io_timers_channel_mapping =
	initIOTimerChannelMapping(io_timers, timer_io_channels);
