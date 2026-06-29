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

/**
 * @file sam_gpiosetevent.c
 *
 * GPIO interrupt wrapper for SAMV7 - maps PX4's gpiosetevent API to SAMV7's gpioirq API
 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <arch/board/board.h>
#include <sam_gpio.h>
#include <errno.h>

/****************************************************************************
 * Name: sam_gpiosetevent
 *
 * Description:
 *   Configure GPIO interrupt for SAMV7.
 *   This function adapts PX4's generic gpiosetevent API to SAMV7's gpio interrupt model.
 *
 * Parameters:
 *   pinset      - GPIO pin configuration (port, pin, mode)
 *   risingedge  - Enable interrupt on rising edge
 *   fallingedge - Enable interrupt on falling edge
 *   event       - Enable interrupt on both edges (overrides rising/falling if true)
 *   handler     - Interrupt handler function
 *   arg         - Argument to pass to handler
 *
 * Returns:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int sam_gpiosetevent(gpio_pinset_t pinset, bool risingedge, bool fallingedge,
                     bool event, xcpt_t handler, void *arg)
{
	gpio_pinset_t intcfg;
	int ret;

	/* Determine the interrupt mode based on the flags */
	if (event) {
		/* Both edges */
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;
	} else if (risingedge && fallingedge) {
		/* Both edges */
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;
	} else if (risingedge) {
		/* Rising edge only */
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_RISING;
	} else if (fallingedge) {
		/* Falling edge only */
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_FALLING;
	} else {
		/* No interrupt */
		return -EINVAL;
	}

	/* Configure the pin as an input with interrupt */
	intcfg = (intcfg & ~GPIO_MODE_MASK) | GPIO_INPUT;

	/* Configure the GPIO */
	ret = sam_configgpio(intcfg);

	if (ret < 0) {
		return ret;
	}

	/* Configure and enable the interrupt if handler is provided */
	if (handler != NULL) {
		/* Configure the GPIO interrupt - this sets up the PIO controller */
		sam_gpioirq(intcfg);

		/* The actual IRQ attachment must be done at the port level
		 * by the caller using irq_attach() with SAM_IRQ_PIOx
		 * This function only configures the pin-level interrupt settings
		 */
		sam_gpioirqenable(intcfg);
	} else {
		/* Disable the interrupt */
		sam_gpioirqdisable(intcfg);
	}

	return OK;
}
