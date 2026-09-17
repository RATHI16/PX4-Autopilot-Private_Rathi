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
 * heater_test - bench validation command for the IMU heater on PE5.
 *
 * PE5 (PE5_HEATR_SNS) drives the gate of Q1 (BSS138 N-MOSFET) through a
 * 100R series resistor with a 100k gate-to-GND pulldown. Q1 low-side switches
 * the R7+R8 (20R, ~0.5W) resistive heater element off VCC_3V3, sitting next to
 * the IMU. PE5 HIGH -> Q1 on -> heater energised; PE5 LOW -> heater off.
 *
 * This is a manual on/off/pulse/pwm test tool for validating the heater
 * circuit on the bench. It does NOT implement closed-loop temperature
 * regulation - it just toggles the GPIO so you can confirm the element warms.
 *
 * Usage:
 *   heater_test on              turn the heater fully on (PE5 high)
 *   heater_test off             turn the heater off (PE5 low)
 *   heater_test status          print the current PE5 output state
 *   heater_test pulse <sec>     on for <sec> seconds, then auto-off
 *   heater_test pwm <duty> <sec>  software PWM at <duty>% for <sec> seconds
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/time.h>

#include <board_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_heater_configured = false;

static void heater_configure(void)
{
	if (!g_heater_configured) {
		px4_arch_configgpio(GPIO_HEATER);
		g_heater_configured = true;
	}
}

static void heater_set(bool on)
{
	heater_configure();
	px4_arch_gpiowrite(GPIO_HEATER, on ? 1 : 0);
}

static void usage(void)
{
	printf("Usage: heater_test <command>\n");
	printf("  on              - heater ON  (PE5 high, Q1 conducts)\n");
	printf("  off             - heater OFF (PE5 low)\n");
	printf("  status          - print current PE5 output state\n");
	printf("  pulse <sec>     - ON for <sec> seconds, then auto-off\n");
	printf("  pwm <duty> <sec>- software PWM at <duty>%% (0-100) for <sec> seconds\n");
}

__EXPORT int heater_test_main(int argc, char *argv[]);

int heater_test_main(int argc, char *argv[])
{
	if (argc < 2) {
		usage();
		return 1;
	}

	if (strcmp(argv[1], "on") == 0) {
		heater_set(true);
		printf("[heater] ON  - PE5 driven HIGH, R7/R8 heater energised (~165 mA @ 3.3V)\n");
		return 0;

	} else if (strcmp(argv[1], "off") == 0) {
		heater_set(false);
		printf("[heater] OFF - PE5 driven LOW, heater de-energised\n");
		return 0;

	} else if (strcmp(argv[1], "status") == 0) {
		heater_configure();
		bool state = px4_arch_gpioread(GPIO_HEATER);
		printf("[heater] PE5 = %s (heater %s)\n", state ? "HIGH" : "LOW",
		       state ? "ON" : "OFF");
		return 0;

	} else if (strcmp(argv[1], "pulse") == 0) {
		if (argc < 3) {
			printf("[heater] pulse needs <sec>\n");
			return 1;
		}

		int sec = atoi(argv[2]);

		if (sec <= 0) {
			printf("[heater] invalid duration\n");
			return 1;
		}

		printf("[heater] pulse ON for %d s...\n", sec);
		heater_set(true);
		px4_usleep((useconds_t)sec * 1000000UL);
		heater_set(false);
		printf("[heater] pulse done - heater OFF\n");
		return 0;

	} else if (strcmp(argv[1], "pwm") == 0) {
		if (argc < 4) {
			printf("[heater] pwm needs <duty> <sec>\n");
			return 1;
		}

		int duty = atoi(argv[2]);
		int sec  = atoi(argv[3]);

		if (duty < 0) { duty = 0; }

		if (duty > 100) { duty = 100; }

		if (sec <= 0) {
			printf("[heater] invalid duration\n");
			return 1;
		}

		/* Software PWM at 100 Hz (10 ms period) for the requested duration. */
		const unsigned period_us = 10000;
		unsigned on_us  = (period_us * (unsigned)duty) / 100u;
		unsigned off_us = period_us - on_us;
		unsigned cycles = (unsigned)sec * (1000000u / period_us);

		printf("[heater] PWM %d%% for %d s (100 Hz software PWM)...\n", duty, sec);
		heater_configure();

		for (unsigned i = 0; i < cycles; i++) {
			if (on_us)  { px4_arch_gpiowrite(GPIO_HEATER, 1); px4_usleep(on_us); }

			if (off_us) { px4_arch_gpiowrite(GPIO_HEATER, 0); px4_usleep(off_us); }
		}

		px4_arch_gpiowrite(GPIO_HEATER, 0);
		printf("[heater] PWM done - heater OFF\n");
		return 0;
	}

	usage();
	return 1;
}
