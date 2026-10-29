/****************************************************************************
 * boards/microchip/samv71-xult-clickboards/src/sam_mpuinit.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdint.h>

#include "mpu.h"
#include "arm_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_mpu_nocache_init
 *
 * Description:
 *   Configure the MPU to mark the dedicated DMA buffer region as non-cacheable.
 *   This must be called from board init AFTER the NuttX MPU has been initialized
 *   and enabled, as we're adding an additional region.
 *
 ****************************************************************************/

void board_mpu_nocache_init(void)
{
  /* Configure the 64KB region at the end of SRAM (0x20450000) as
   * Normal, Shareable, Non-Cacheable memory.
   *
   * CRITICAL FIX: Use a hardcoded HIGH region number (14) to ensure priority
   * over the default SRAM region (usually Region 2 or 3). Using mpu_configure_region()
   * allocates the next available region (likely 0), which would be overridden
   * by the default cacheable map, rendering this configuration useless.
   */

  uint32_t region_base = 0x2045E000;
  uint32_t region_num  = 14; /* High priority region (Max is usually 15) */
  uint32_t l2size      = 13; /* 8KB = 2^13 */
  uint32_t regval;

  /* Select Region 14 */
  putreg32(region_num, MPU_RNR);

  /* Set Base Address and Valid bit */
  putreg32(region_base | region_num | MPU_RBAR_VALID, MPU_RBAR);

  /* Set Attributes and Size */
  /* TEX=001 (Normal), S=1 (Shareable), C=0 (NC), B=0 (NB), AP=RW/RW, XN=0 */
  regval = MPU_RASR_ENABLE                              | /* Enable region */
           MPU_RASR_SIZE_LOG2(l2size)                   | /* 64KB */
           MPU_RASR_TEX_NOR                             | /* Normal memory */
           MPU_RASR_S                                   | /* Shareable */
           MPU_RASR_AP_RWRW;                              /* P:RW U:RW */

  putreg32(regval, MPU_RASR);
}
