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

/*
 * SAMV7 tone alarm interface for the GPS-module buzzer on PB1.
 *
 * PB1 has NO timer-output (TIOA/TIOB) function and its only PWM-controller
 * mapping (PWMC0 channel 1, Peripheral A) is already owned by motor 2 (PA2),
 * so hardware PWM cannot be used to drive the buzzer independently.
 *
 * Instead we generate the note frequency in software: a spare Timer Counter
 * channel (TC0 channel 2, PID 25 / SAM_IRQ_TC2 — free; HRT uses TC0 ch0 and
 * the motors use PWMC0) raises an RC-compare interrupt at twice the requested
 * note frequency, and the ISR toggles the PB1 GPIO. That produces a square
 * wave at the exact pitch, so the (passive) buzzer plays the same melodic
 * tunes as a Pixhawk instead of a single flat tone.
 */

#include <drivers/drv_tone_alarm.h>
#include <px4_platform_common/defines.h>
#include <board_config.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "hardware/sam_tc.h"
#include "hardware/sam_pmc.h"
#include "sam_gpio.h"

#include <arch/chip/irq.h>   /* SAM_IRQ_TC2, SAM_PID_TC2 */

namespace
{

/* TC0 channel 2 — free channel used for tone generation.
 * SAM_TC012_BASE = 0x4000c000, channel 2 offset = (2 << 6) = 0x80.
 */
constexpr uint32_t TONE_TC_BASE = SAM_TC012_BASE + SAM_TC_CHAN_OFFSET(2);
constexpr int      TONE_TC_IRQ  = SAM_IRQ_TC2;
constexpr int      TONE_TC_PID  = SAM_PID_TC2;

/* Timer input clock = MCK / 128 = 150 MHz / 128 = 1,171,875 Hz.
 * The /128 prescale keeps the RC period inside 16 bits for the whole tune
 * frequency range (a ~50 Hz note needs RC = 11719).
 */
constexpr uint32_t TONE_TC_CLOCK = BOARD_MCK_FREQUENCY / 128;

bool          g_initialized = false;
volatile bool g_level       = false;

inline void tc_putreg(uint32_t offset, uint32_t value)
{
	putreg32(value, TONE_TC_BASE + offset);
}

inline uint32_t tc_getreg(uint32_t offset)
{
	return getreg32(TONE_TC_BASE + offset);
}

/* RC-compare ISR: toggle the buzzer GPIO to build the square wave. */
int tone_tc_isr(int irq, void *context, void *arg)
{
	/* Reading SR clears the pending RC-compare (CPCS) status. */
	(void)tc_getreg(SAM_TC_SR_OFFSET);

	g_level = !g_level;
	sam_gpiowrite(GPIO_TONE_ALARM_GPIO, g_level);
	return OK;
}

} /* anonymous namespace */

namespace ToneAlarmInterface
{

void init()
{
	/* Buzzer GPIO to the idle (off) state. */
	px4_arch_configgpio(GPIO_TONE_ALARM_IDLE);

	/* Enable the TC0 ch2 peripheral clock (PID 25) via PMC. */
	uint32_t regval = getreg32(SAM_PMC_PCER0);
	regval |= (1u << TONE_TC_PID);
	putreg32(regval, SAM_PMC_PCER0);

	/* Stop the channel and mask its interrupts until a note is played. */
	tc_putreg(SAM_TC_CCR_OFFSET, TC_CCR_CLKDIS);
	tc_putreg(SAM_TC_IDR_OFFSET, TC_INT_ALL);

	/* Attach and enable the TC0 ch2 vector. Gating is done with the
	 * channel clock (CLKEN/CLKDIS) and IER/IDR, so it is safe to leave
	 * the NVIC line enabled here.
	 */
	irq_attach(TONE_TC_IRQ, tone_tc_isr, nullptr);
	up_enable_irq(TONE_TC_IRQ);

	g_initialized = true;
}

hrt_abstime start_note(unsigned frequency)
{
	if (!g_initialized || frequency == 0) {
		return hrt_absolute_time();
	}

	/* Interrupt at 2x the note frequency (one toggle per half period). */
	uint32_t rc = TONE_TC_CLOCK / (2u * frequency);

	if (rc < 2) {
		rc = 2;                 /* clamp absurdly high pitches */

	} else if (rc > 0xffff) {
		rc = 0xffff;            /* keep inside a 16-bit TC counter */
	}

	irqstate_t flags = enter_critical_section();

	/* Disable while reconfiguring. */
	tc_putreg(SAM_TC_CCR_OFFSET, TC_CCR_CLKDIS);
	tc_putreg(SAM_TC_IDR_OFFSET, TC_INT_ALL);

	/* Waveform mode, clock = MCK/128, count up and auto-reset on RC
	 * compare. The TIOA/TIOB pin outputs are unused (PB1 is driven as a
	 * plain GPIO from the ISR), so no ACPA/ACPC bits are needed.
	 */
	uint32_t cmr = TC_CMR_TCCLKS_MCK128 | TC_CMR_WAVE | TC_CMR_WAVSEL_UPRC;
	tc_putreg(SAM_TC_CMR_OFFSET, cmr);
	tc_putreg(SAM_TC_RC_OFFSET, rc);

	/* Start from a known GPIO level and enable RC-compare interrupt. */
	g_level = false;
	sam_gpiowrite(GPIO_TONE_ALARM_GPIO, g_level);

	tc_putreg(SAM_TC_IER_OFFSET, TC_INT_CPCS);
	tc_putreg(SAM_TC_CCR_OFFSET, TC_CCR_CLKEN | TC_CCR_SWTRG);

	leave_critical_section(flags);

	return hrt_absolute_time();
}

void stop_note()
{
	if (!g_initialized) {
		return;
	}

	irqstate_t flags = enter_critical_section();

	/* Stop the timer, mask its interrupt, and force the buzzer off. */
	tc_putreg(SAM_TC_IDR_OFFSET, TC_INT_ALL);
	tc_putreg(SAM_TC_CCR_OFFSET, TC_CCR_CLKDIS);
	sam_gpiowrite(GPIO_TONE_ALARM_GPIO, false);

	g_level = false;

	leave_critical_section(flags);
}

} /* namespace ToneAlarmInterface */
