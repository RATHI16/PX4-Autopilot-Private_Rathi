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
 * @file board_reset.cpp
 * SAMV7 Board RESET API — uses GPBR0 for boot mode persistence across resets.
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/shutdown.h>
#include <systemlib/px4_macros.h>
#include <errno.h>
#include <nuttx/board.h>
#include <nuttx/arch.h>
#include "arm_internal.h"

#ifdef CONFIG_BOARDCTL_RESET

/* SAMV7 General Purpose Backup Register 0 — survives system reset */
#define SAMV7_GPBR_BASE      0x400E1890
#define SAMV7_BOOT_MODE_REG  (SAMV7_GPBR_BASE + 0)  /* GPBR0 */

static const uint32_t modes[] = {
	/*                                      to  tb   */
	/* BOARD_RESET_MODE_CLEAR                5   y   */  0,
	/* BOARD_RESET_MODE_BOOT_TO_BL           0   n   */  0xb007b007,
	/* BOARD_RESET_MODE_BOOT_TO_VALID_APP    0   y   */  0xb0070002,
	/* BOARD_RESET_MODE_CAN_BL               10  n   */  0xb0080000,
	/* BOARD_RESET_MODE_RTC_BOOT_FWOK        0   n   */  0xb0093a26
};

int board_configure_reset(reset_mode_e mode, uint32_t arg)
{
	if (mode < arraySize(modes)) {
		arg = mode == BOARD_RESET_MODE_CAN_BL ? arg & ~0xff : 0;
		putreg32(modes[mode] | arg, SAMV7_BOOT_MODE_REG);
		return OK;
	}

	return -EINVAL;
}

int board_reset(int status)
{
	if (status == REBOOT_TO_BOOTLOADER) {
		board_configure_reset(BOARD_RESET_MODE_BOOT_TO_BL, 0);
	}

#if defined(BOARD_HAS_ON_RESET)
	board_on_reset(status);
#endif

	up_systemreset();

	return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */
