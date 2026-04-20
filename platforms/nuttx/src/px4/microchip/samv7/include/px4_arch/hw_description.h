/****************************************************************************
 *
 *   Copyright (C) 2024 PX4 Development Team. All rights reserved.
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

#pragma once

#include <stdint.h>
#include <px4_platform_common/constexpr_util.h>

/*
 * SAMV7 Timer/Counter Hardware Description
 *
 * SAMV7 has 4 TC blocks (TC0-TC3), each with 3 channels:
 * - TC0: channels TC0, TC1, TC2 (base 0x4000C000)
 * - TC1: channels TC3, TC4, TC5 (base 0x40010000)
 * - TC2: channels TC6, TC7, TC8 (base 0x40014000)
 * - TC3: channels TC9, TC10, TC11 (base 0x40054000)
 */

namespace Timer
{
enum Timer {
	Timer1 = 1,   /* TC0 CH1 - PA15 */
	Timer2 = 2,   /* TC0 CH2 - PA26 */
	Timer3 = 3,   /* TC1 CH0 - PC23 */
	Timer4 = 4,   /* TC1 CH1 - PC26 */
	Timer5 = 5,   /* TC1 CH2 - PC29 */
	Timer6 = 6,   /* TC2 CH0 - PC5  */
};

enum Channel {
	Channel1 = 1,
	Channel2 = 2,
	Channel3 = 3,
};

struct TimerChannel {
	Timer timer;
	Channel channel;
};
}

/* TC block base addresses - use NuttX definitions */
#include "hardware/samv71_memorymap.h"
#define TC_CHAN_OFFSET  0x40        /* Channel offset within block */

static inline constexpr uint32_t timerBaseRegister(Timer::Timer timer)
{
	switch (timer) {
	case Timer::Timer1: return SAM_TC012_BASE + (1 * TC_CHAN_OFFSET); /* TC0 CH1 */
	case Timer::Timer2: return SAM_TC012_BASE + (2 * TC_CHAN_OFFSET); /* TC0 CH2 */
	case Timer::Timer3: return SAM_TC345_BASE + (0 * TC_CHAN_OFFSET); /* TC1 CH0 */
	case Timer::Timer4: return SAM_TC345_BASE + (1 * TC_CHAN_OFFSET); /* TC1 CH1 */
	case Timer::Timer5: return SAM_TC345_BASE + (2 * TC_CHAN_OFFSET); /* TC1 CH2 */
	case Timer::Timer6: return SAM_TC678_BASE + (0 * TC_CHAN_OFFSET); /* TC2 CH0 */
	}

	return 0;
}

/*
 * SAMV7 PWM Controller (PWMC) Hardware Description
 *
 * SAMV7 has 2 PWMC modules (PWM0, PWM1), each with 4 channels (CH0-CH3):
 * - PWM0: base 0x40020000, PID 31
 * - PWM1: base 0x4005C000, PID 60
 *
 * Each channel has independent period (CPRD) and duty cycle (CDTY) registers.
 * Use CDTYUPD/CPRDUPD for glitch-free updates during PWM operation.
 */
namespace PWM
{
enum PWMModule {
	PWM0 = 0,
	PWM1 = 1,
};

enum Channel {
	Channel0 = 0,
	Channel1 = 1,
	Channel2 = 2,
	Channel3 = 3,
};

struct PWMChannel {
	PWMModule module;
	Channel channel;
};
}

/* Peripheral function for PWMC GPIO pins (A or B depending on pin) */
enum class PWMCPeripheral {
	A = 0,  /* GPIO_PERIPHA */
	B = 1,  /* GPIO_PERIPHB */
};

/* PWMC base addresses - NuttX may define these in hardware/sam_memorymap.h */
#ifndef SAM_PWM0_BASE
#define SAM_PWM0_BASE  0x40020000
#endif
#ifndef SAM_PWM1_BASE
#define SAM_PWM1_BASE  0x4005C000
#endif

static inline constexpr uint32_t pwmBaseRegister(PWM::PWMModule module)
{
	switch (module) {
	case PWM::PWM0: return SAM_PWM0_BASE;
	case PWM::PWM1: return SAM_PWM1_BASE;
	}

	return 0;
}

/*
 * GPIO
 */
namespace GPIO
{

enum Port {
	PortA = 0,
	PortB,
	PortC,
	PortD,
	PortE,
};

enum Pin {
	Pin0 = 0, Pin1, Pin2, Pin3, Pin4, Pin5, Pin6, Pin7,
	Pin8, Pin9, Pin10, Pin11, Pin12, Pin13, Pin14, Pin15,
	Pin16, Pin17, Pin18, Pin19, Pin20, Pin21, Pin22, Pin23,
	Pin24, Pin25, Pin26, Pin27, Pin28, Pin29, Pin30, Pin31,
};

struct GPIOPin {
	Port port;
	Pin pin;
};
}

static inline constexpr uint32_t getGPIOPort(GPIO::Port port)
{
	switch (port) {
	case GPIO::PortA: return (0 << 5);
	case GPIO::PortB: return (1 << 5);
	case GPIO::PortC: return (2 << 5);
	case GPIO::PortD: return (3 << 5);
	case GPIO::PortE: return (4 << 5);
	}

	return 0;
}

static inline constexpr uint32_t getGPIOPin(GPIO::Pin pin)
{
	return (uint32_t)pin;
}

/*
 * SPI
 */
namespace SPI
{

enum class Bus {
	SPI0 = 1,  // PX4 uses 1-based, NuttX SAMV7 uses 0-based (converted in micro_hal.h)
	SPI1 = 2,
};

using CS = GPIO::GPIOPin;
using DRDY = GPIO::GPIOPin;

struct bus_device_external_cfg_t {
	CS cs_gpio;
	DRDY drdy_gpio;
};

} // namespace SPI