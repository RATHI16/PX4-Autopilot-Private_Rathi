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
 * GPIO interrupt wrapper for SAMV7 — works WITH NuttX's existing
 * port-level ISR dispatchers (sam_gpioirq.c) via per-pin virtual IRQs.
 */

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <arch/board/board.h>
#include <sam_gpio.h>
#include <errno.h>

/**
 * Convert a gpio_pinset_t to its NuttX virtual IRQ number.
 * NuttX installs port-level handlers in sam_gpioirqinitialize() that
 * read PIO_ISR and call irq_dispatch(SAM_IRQ_Px0 + pin) for each set bit.
 * We attach our handler to that per-pin virtual IRQ.
 */
static int pinset_to_virq(gpio_pinset_t pinset)
{
	int pin  = (pinset & GPIO_PIN_MASK) >> GPIO_PIN_SHIFT;
	int port = (pinset & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT;

	switch (port) {
#ifdef CONFIG_SAMV7_GPIOA_IRQ
	case 0: return SAM_IRQ_PA0 + pin;
#endif
#ifdef CONFIG_SAMV7_GPIOB_IRQ
	case 1: return SAM_IRQ_PB0 + pin;
#endif
#ifdef CONFIG_SAMV7_GPIOC_IRQ
	case 2: return SAM_IRQ_PC0 + pin;
#endif
#ifdef CONFIG_SAMV7_GPIOD_IRQ
	case 3: return SAM_IRQ_PD0 + pin;
#endif
#ifdef CONFIG_SAMV7_GPIOE_IRQ
	case 4: return SAM_IRQ_PE0 + pin;
#endif
	default: return -EINVAL;
	}
}

int sam_gpiosetevent(gpio_pinset_t pinset, bool risingedge, bool fallingedge,
		     bool event, xcpt_t handler, void *arg)
{
	gpio_pinset_t intcfg;

	/* Determine interrupt edge mode */
	if (event) {
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;

	} else if (risingedge && fallingedge) {
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;

	} else if (risingedge) {
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_RISING;

	} else if (fallingedge) {
		intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_FALLING;

	} else {
		return -EINVAL;
	}

	intcfg = (intcfg & ~GPIO_MODE_MASK) | GPIO_INPUT;

	/* Convert to virtual IRQ */
	int virq = pinset_to_virq(pinset);

	if (virq < 0) {
		return virq;
	}

	/* Configure the GPIO pin for interrupt */
	int ret = sam_configgpio(intcfg);

	if (ret < 0) {
		return ret;
	}

	sam_gpioirq(intcfg);

	if (handler) {
		ret = irq_attach(virq, handler, arg);

		if (ret < 0) {
			return ret;
		}

		sam_gpioirqenable(virq);

	} else {
		sam_gpioirqdisable(virq);
		irq_attach(virq, NULL, NULL);
	}

	return OK;
}
