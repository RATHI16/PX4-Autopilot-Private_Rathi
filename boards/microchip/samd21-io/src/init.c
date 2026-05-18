/****************************************************************************
 * boards/microchip/samd21-io/src/init.c
 *
 * Board initialization for the SAMD21J18A PX4IO co-processor.
 * Called by NuttX early in startup, before user_start().
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>

#include <stdbool.h>
#include <stdio.h>
#include <debug.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

#include <sam_port.h>
#include "board_config.h"

__EXPORT void sam_boardinitialize(void)
{
	/* LEDs — default off */
	sam_configport(GPIO_LED_BLUE);
	sam_portwrite(GPIO_LED_BLUE, false);

	sam_configport(GPIO_LED_AMBER);
	sam_portwrite(GPIO_LED_AMBER, false);

	sam_configport(GPIO_LED_SAFETY);
	sam_portwrite(GPIO_LED_SAFETY, false);

	sam_configport(GPIO_LED_GREEN);
	sam_portwrite(GPIO_LED_GREEN, false);

	/* Safety switch input */
	sam_configport(GPIO_BTN_SAFETY);

	/* Servo fault detect input */
	sam_configport(GPIO_SERVO_FAULT_DETECT);

	/* SBUS output enable — disable by default (active low, so set high) */
	sam_portwrite(GPIO_SBUS_OENABLE, true);
	sam_configport(GPIO_SBUS_OENABLE);

	/* Spektrum power enable — on by default */
	sam_portwrite(GPIO_SPEKTRUM_PWR_EN, true);
	sam_configport(GPIO_SPEKTRUM_PWR_EN);

	/* Spektrum bind line — deassert (high) */
	SPEKTRUM_RX_AS_UART();

	/* PWM output pins — configure as TCC outputs (low on boot) */
	sam_configport(GPIO_PWM1);
	sam_configport(GPIO_PWM2);
	sam_configport(GPIO_PWM3);
	sam_configport(GPIO_PWM4);
	sam_configport(GPIO_PWM5);
	sam_configport(GPIO_PWM6);
	sam_configport(GPIO_PWM7);
	sam_configport(GPIO_PWM8);
}
