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

#include <sam_tc.h>
#include <sam_spi.h>
#include <sam_twihs.h>
#include <sam_gpio.h>

/* SAMV7 defines the 96 bit UUID as
 *  uint32_t[3] that can be read as bytes/half-words/words
 *  uint32_t[0] PX4_CPU_UUID_ADDRESS[0] bits 31:0  (offset 0)
 *  uint32_t[1] PX4_CPU_UUID_ADDRESS[1] bits 63:32 (offset 4)
 *  uint32_t[2] PX4_CPU_UUID_ADDRESS[3] bits 96:64 (offset 8)
 *
 * The UUID is read from the CHIPID registers (CIDR and EXID).
 * For compatibility with PX4 legacy implementations, we maintain
 * the standard UUID format where ABCD EFGH IJKL represents:
 *       A was bit 31 and D was bit 0
 *       E was bit 63 and H was bit 32
 *       I was bit 95 and L was bit 64
 *
 * For new targets moving forward we will use
 *      IJKL EFGH ABCD
 */
#define PX4_CPU_UUID_BYTE_LENGTH                12
#define PX4_CPU_UUID_WORD32_LENGTH              (PX4_CPU_UUID_BYTE_LENGTH/sizeof(uint32_t))

/* The mfguid will be an array of bytes with
 * MSD @ index 0 - LSD @ index PX4_CPU_MFGUID_BYTE_LENGTH-1
 *
 * It will be converted to a string with the MSD on left and LSD on the right most position.
 */
#define PX4_CPU_MFGUID_BYTE_LENGTH              PX4_CPU_UUID_BYTE_LENGTH

/* By not defining PX4_CPU_UUID_CORRECT_CORRELATION the following maintains the legacy incorrect order
 * used for selection of significant digits of the UUID in the PX4 code base.
 * This is done to avoid the ripple effects changing the IDs used on existing platforms
 */
#if defined(PX4_CPU_UUID_CORRECT_CORRELATION)
# define PX4_CPU_UUID_WORD32_UNIQUE_H            0 /* Least significant digits change the most */
# define PX4_CPU_UUID_WORD32_UNIQUE_M            1 /* Middle significant digits */
# define PX4_CPU_UUID_WORD32_UNIQUE_L            2 /* Most significant digits change the least */
#else
/* Legacy incorrect ordering */
# define PX4_CPU_UUID_WORD32_UNIQUE_H            2 /* Most significant digits change the least */
# define PX4_CPU_UUID_WORD32_UNIQUE_M            1 /* Middle significant digits */
# define PX4_CPU_UUID_WORD32_UNIQUE_L            0 /* Least significant digits change the most */
#endif

/*                                                  Separator    nnn:nnn:nnnn     2 char per byte           term */
#define PX4_CPU_UUID_WORD32_FORMAT_SIZE         (PX4_CPU_UUID_WORD32_LENGTH-1+(2*PX4_CPU_UUID_BYTE_LENGTH)+1)
#define PX4_CPU_MFGUID_FORMAT_SIZE              ((2*PX4_CPU_MFGUID_BYTE_LENGTH)+1)

/* SAMV7 does not have battery-backed SRAM like STM32, but has GPBR (General Purpose Backup Registers)
 * For now, savepanic is not implemented. This can be added later using GPBR if needed.
 */
#define px4_savepanic(fileno, context, length)  (0)

#define PX4_BUS_OFFSET       1                  /* PX4 uses 1-based, NuttX SAMV7 uses 0-based */
#define px4_spibus_initialize(bus_num_1based)   sam_spibus_initialize((bus_num_1based) - 1)

#define px4_i2cbus_initialize(bus_num_1based)   sam_i2cbus_initialize((bus_num_1based) - 1)
#define px4_i2cbus_uninitialize(pdev)           sam_i2cbus_uninitialize(pdev)

#define px4_arch_configgpio(pinset)             sam_configgpio(pinset)
#define px4_arch_unconfiggpio(pinset)           sam_unconfiggpio(pinset)
#define px4_arch_gpioread(pinset)               sam_gpioread(pinset)
#define px4_arch_gpiowrite(pinset, value)       sam_gpiowrite(pinset, value)

/* GPIO interrupt configuration - implementation provided in board files */
#define px4_arch_gpiosetevent(pinset,r,f,e,fp,a)  sam_gpiosetevent(pinset,r,f,e,fp,a)

/* Forward declaration - actual implementation in board support files */
int sam_gpiosetevent(gpio_pinset_t pinset, bool risingedge, bool fallingedge,
                     bool event, xcpt_t handler, void *arg);

#define PX4_MAKE_GPIO_INPUT(gpio) (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INPUT|GPIO_CFG_PULLUP))
#define PX4_MAKE_GPIO_EXTI(gpio) (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INT|GPIO_INPUT|GPIO_PULLUP))
#define PX4_MAKE_GPIO_OUTPUT_CLEAR(gpio) (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_OUTPUT|GPIO_OUTPUT_CLEAR))
#define PX4_MAKE_GPIO_OUTPUT_SET(gpio) (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_OUTPUT|GPIO_OUTPUT_SET))

#define PX4_GPIO_PIN_OFF(def) (((def) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INPUT|GPIO_FLOAT))

/* CAN bootloader usage - SAMV7 has MCAN but not yet configured
 * These definitions are placeholders for future CAN support
 */

/* SAMV7 runs at 150MHz (MCK = Master Clock)
 * BOARD_MCK_FREQUENCY is defined in board.h
 */
#define TIMER_HRT_CYCLES_PER_US (BOARD_MCK_FREQUENCY/1000000)
#define TIMER_HRT_CYCLES_PER_MS (BOARD_MCK_FREQUENCY/1000)

/* CAN filter registers - not implemented yet for SAMV7 MCAN
 * These are placeholders for future CAN bootloader support
 */
#define crc_HiLOC       0
#define crc_LoLOC       0
#define signature_LOC   0
#define bus_speed_LOC   0
#define node_id_LOC     0

#if defined(CONFIG_ARMV7M_DCACHE)
#  define PX4_ARCH_DCACHE_ALIGNMENT ARMV7M_DCACHE_LINESIZE
#  define px4_cache_aligned_data() aligned_data(ARMV7M_DCACHE_LINESIZE)
#  define px4_cache_aligned_alloc(s) memalign(ARMV7M_DCACHE_LINESIZE,(s))
#else
#  define px4_cache_aligned_data()
#  define px4_cache_aligned_alloc malloc
#endif


__END_DECLS
