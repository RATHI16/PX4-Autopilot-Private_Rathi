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
#pragma once

#include <px4_platform/micro_hal.h>

__BEGIN_DECLS

/* Forward-declare NuttX I2C type so px4_mtd.h compiles without CONFIG_I2C */
struct i2c_master_s;

#include <sam_port.h>

/*
 * SAMD21 / SAMD2L2 GPIO abstraction.
 * NuttX uses port_pinset_t and sam_configport / sam_portwrite / sam_portread.
 */
#define PX4_BUS_OFFSET       0
#define px4_spibus_initialize(bus_num_1based)   NULL  /* no SPI needed for PX4IO */
#define px4_i2cbus_initialize(bus_num_1based)   NULL
#define px4_i2cbus_uninitialize(pdev)           do {} while(0)

#define px4_arch_configgpio(pinset)             sam_configport(pinset)
#define px4_arch_unconfiggpio(pinset)           sam_configport(pinset)
#define px4_arch_gpioread(pinset)               sam_portread(pinset)
#define px4_arch_gpiowrite(pinset, value)       sam_portwrite(pinset, value)

/* GPIO interrupt — not required for PX4IO minimal build */
#define px4_arch_gpiosetevent(pinset,r,f,e,fp,a)  (-ENOSYS)

#define px4_savepanic(fileno, context, length)  (0)

/* UUID — SAMD21 has a 128-bit serial number at fixed addresses */
#define PX4_CPU_UUID_BYTE_LENGTH                16
#define PX4_CPU_UUID_WORD32_LENGTH              (PX4_CPU_UUID_BYTE_LENGTH / sizeof(uint32_t))
#define PX4_CPU_MFGUID_BYTE_LENGTH              PX4_CPU_UUID_BYTE_LENGTH
#define PX4_CPU_UUID_WORD32_UNIQUE_H            3
#define PX4_CPU_UUID_WORD32_UNIQUE_M            2
#define PX4_CPU_UUID_WORD32_UNIQUE_L            1
#define PX4_CPU_UUID_WORD32_FORMAT_SIZE         (PX4_CPU_UUID_WORD32_LENGTH - 1 + (2 * PX4_CPU_UUID_BYTE_LENGTH) + 1)
#define PX4_CPU_MFGUID_FORMAT_SIZE              ((2 * PX4_CPU_MFGUID_BYTE_LENGTH) + 1)

/* SAMD21 runs at 48 MHz */
#define TIMER_HRT_CYCLES_PER_US  (BOARD_CPU_FREQUENCY / 1000000)
#define TIMER_HRT_CYCLES_PER_MS  (BOARD_CPU_FREQUENCY / 1000)

/* No D-cache on Cortex-M0+ */
#define px4_cache_aligned_data()
#define px4_cache_aligned_alloc  malloc

/* GPIO helpers */
#define PX4_MAKE_GPIO_INPUT(gpio)         (((gpio) & (PORT_PORTMASK | PORT_PINMASK)) | PORT_INPUT)
#define PX4_MAKE_GPIO_OUTPUT_CLEAR(gpio)  (((gpio) & (PORT_PORTMASK | PORT_PINMASK)) | PORT_OUTPUT | PORT_OUTVALUE_LOW)
#define PX4_MAKE_GPIO_OUTPUT_SET(gpio)    (((gpio) & (PORT_PORTMASK | PORT_PINMASK)) | PORT_OUTPUT | PORT_OUTVALUE_HIGH)

__END_DECLS
