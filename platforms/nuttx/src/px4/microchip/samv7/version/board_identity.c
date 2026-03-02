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
 * @file board_identity.c
 * Implementation of SAMV7 based Board identity API using true 128-bit Unique ID.
 *
 * SAMV71 has a per-chip 128-bit unique ID readable via EEFC STUI/SPUI commands.
 * During STUI, flash is overlaid with UID data — code executing from flash will
 * hardfault. read_uniqueid_128() is marked __ramfunc__ to execute from SRAM,
 * mirroring NuttX's sam_eefc_readsequence() approach. Only inline functions
 * (getreg32/putreg32/up_irq_save/up_irq_restore) are called within the
 * critical STUI-SPUI window.
 * We use the first 12 bytes (3 words) to match PX4_CPU_UUID_BYTE_LENGTH=12.
 */

#include <px4_platform_common/px4_config.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <nuttx/arch.h>
#include "arm_internal.h"
#include "hardware/sam_eefc.h"

#define CPU_UUID_BYTE_FORMAT_ORDER          {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8}
#define SWAP_UINT32(x) (((x) >> 24) | (((x) & 0x00ff0000) >> 8) | (((x) & 0x0000ff00) << 8) | ((x) << 24))

/* SAMV7 SoC Architecture ID */
#ifndef PX4_SOC_ARCH_ID
#define PX4_SOC_ARCH_ID 0x0017  /* Microchip SAMV7 */
#endif

static const uint16_t soc_arch_id = PX4_SOC_ARCH_ID;

/* A type suitable for holding the reordering array for the byte format of the UUID */
typedef const uint8_t uuid_uint8_reorder_t[PX4_CPU_UUID_BYTE_LENGTH];

/* EEFC flash command IDs for reading Unique ID */
#ifndef FCMD_STUI
#define FCMD_STUI  0x000E  /* Start Read Unique Identifier */
#endif
#ifndef FCMD_SPUI
#define FCMD_SPUI  0x000F  /* Stop Read Unique Identifier */
#endif

/* EEFC flash base for reading mapped unique ID data */
#ifndef SAM_INTFLASH_BASE
#define SAM_INTFLASH_BASE 0x00400000
#endif

/**
 * Read the SAMV71's true 128-bit per-chip unique ID via EEFC STUI/SPUI.
 *
 * CRITICAL: This function MUST execute from SRAM (__ramfunc__) because
 * between STUI and SPUI commands, internal flash is overlaid with UID data.
 * Any instruction fetch from flash during this window will hardfault.
 *
 * Only uses inline functions (getreg32/putreg32/up_irq_save/up_irq_restore)
 * to avoid calling flash-resident library code.
 * Mirrors NuttX's sam_eefc_readsequence() (sam_eefc.c) approach.
 */
__ramfunc__ static void read_uniqueid_128(uint8_t uid[16])
{
	irqstate_t flags = up_irq_save();

	/* Enable Sequential Code Optimization + set wait states to 4 */
	uint32_t fmr_save = getreg32(SAM_EEFC_FMR);
	putreg32((fmr_save & ~EEFC_FMR_FWS_MASK) | EEFC_FMR_FWS(4) | EEFC_FMR_SCOD, SAM_EEFC_FMR);

	/* STUI (Start Read Unique Identifier) */
	putreg32(EEFC_FCR_FCMD(FCMD_STUI) | EEFC_FCR_FKEY_PASSWD, SAM_EEFC_FCR);

	/* Wait for FRDY to fall (flash now shows UID) */
	while ((getreg32(SAM_EEFC_FSR) & EEFC_FSR_FRDY) != 0) {
	}

	/* Read 4 x 32-bit words from flash base (UID overlay) */
	volatile uint32_t *flash = (volatile uint32_t *)SAM_INTFLASH_BASE;
	uint32_t buffer[4];

	for (int i = 0; i < 4; i++) {
		buffer[i] = flash[i];
	}

	/* SPUI (Stop Read Unique Identifier) — restores normal flash */
	putreg32(EEFC_FCR_FCMD(FCMD_SPUI) | EEFC_FCR_FKEY_PASSWD, SAM_EEFC_FCR);

	/* Wait for FRDY to rise (flash back to normal) */
	while ((getreg32(SAM_EEFC_FSR) & EEFC_FSR_FRDY) == 0) {
	}

	/* Restore original FMR (clears SCOD if it wasn't set) */
	putreg32(fmr_save, SAM_EEFC_FMR);

	up_irq_restore(flags);

	/* Convert to big-endian byte array (matches NuttX sam_uid.c layout) */
	for (int i = 0; i < 4; i++) {
		uid[4 * i + 0] = (uint8_t)(buffer[i] >> 24);
		uid[4 * i + 1] = (uint8_t)(buffer[i] >> 16);
		uid[4 * i + 2] = (uint8_t)(buffer[i] >> 8);
		uid[4 * i + 3] = (uint8_t)(buffer[i]);
	}
}

/**
 * Read the true per-chip unique ID (first 12 bytes of the 128-bit UID).
 */
static void get_chip_uid(uint32_t uid_words[PX4_CPU_UUID_WORD32_LENGTH])
{
	uint8_t uniqueid[16];
	read_uniqueid_128(uniqueid);
	memcpy(uid_words, uniqueid, PX4_CPU_UUID_BYTE_LENGTH);
}

#ifdef CONFIG_BOARDCTL_UNIQUEID
int board_uniqueid(FAR uint8_t *uniqueid)
{
	if (uniqueid == NULL) {
		return -EINVAL;
	}

	read_uniqueid_128(uniqueid);
	return OK;
}
#endif

void board_get_uuid(uuid_byte_t uuid_bytes)
{
	uuid_uint8_reorder_t reorder = CPU_UUID_BYTE_FORMAT_ORDER;
	union {
		uuid_byte_t b;
		uuid_uint32_t w;
	} id;

	board_get_uuid32(id.w);

	for (int i = 0; i < PX4_CPU_UUID_BYTE_LENGTH; i++) {
		uuid_bytes[i] = id.b[reorder[i]];
	}
}

__EXPORT void board_get_uuid32(uuid_uint32_t uuid_words)
{
	get_chip_uid(uuid_words);
}

int board_get_uuid32_formated(char *format_buffer, int size,
			      const char *format,
			      const char *seperator)
{
	uuid_uint32_t uuid;
	board_get_uuid32(uuid);
	int offset = 0;
	int sep_size = seperator ? strlen(seperator) : 0;

	for (unsigned i = 0; (offset < size - 1) && (i < PX4_CPU_UUID_WORD32_LENGTH); i++) {
		offset += snprintf(&format_buffer[offset], size - offset, format, uuid[i]);

		if (sep_size && (offset < size - sep_size - 1) && (i < PX4_CPU_UUID_WORD32_LENGTH - 1)) {
			strncat(&format_buffer[offset], seperator, size - offset);
			offset += sep_size;
		}
	}

	return 0;
}

int board_get_mfguid(mfguid_t mfgid)
{
	uint32_t uid[PX4_CPU_UUID_WORD32_LENGTH];
	get_chip_uid(uid);
	uint8_t *rv = &mfgid[0];

	for (unsigned i = 0; i < PX4_CPU_UUID_WORD32_LENGTH; i++) {
		uint32_t uuid_bytes = SWAP_UINT32(uid[i]);
		memcpy(rv, &uuid_bytes, sizeof(uint32_t));
		rv += sizeof(uint32_t);
	}

	return PX4_CPU_MFGUID_BYTE_LENGTH;
}

int board_get_mfguid_formated(char *format_buffer, int size)
{
	mfguid_t mfguid;

	board_get_mfguid(mfguid);
	int offset  = 0;

	for (unsigned i = 0; offset < size && i < PX4_CPU_MFGUID_BYTE_LENGTH; i++) {
		offset += snprintf(&format_buffer[offset], size - offset, "%02x", mfguid[i]);
	}

	return offset;
}

int board_get_px4_guid(px4_guid_t px4_guid)
{
	uint8_t  *pb = (uint8_t *) &px4_guid[0];
	*pb++ = (soc_arch_id >> 8) & 0xff;
	*pb++ = (soc_arch_id & 0xff);

	for (unsigned i = 0; i < PX4_GUID_BYTE_LENGTH - (sizeof(soc_arch_id) + PX4_CPU_UUID_BYTE_LENGTH); i++) {
		*pb++ = 0;
	}

	uint32_t uid[PX4_CPU_UUID_WORD32_LENGTH];
	get_chip_uid(uid);

	for (unsigned i = 0; i < PX4_CPU_UUID_WORD32_LENGTH; i++) {
		uint32_t uuid_bytes = SWAP_UINT32(uid[i]);
		memcpy(pb, &uuid_bytes, sizeof(uint32_t));
		pb += sizeof(uint32_t);
	}

	return PX4_GUID_BYTE_LENGTH;
}

int board_get_px4_guid_formated(char *format_buffer, int size)
{
	px4_guid_t px4_guid;
	board_get_px4_guid(px4_guid);
	int offset  = 0;

	/* size should be 2 per byte + 1 for termination
	 * So it needs to be odd
	 */
	size = size & 1 ? size : size - 1;

	/* Discard from MSD */
	for (unsigned i = PX4_GUID_BYTE_LENGTH - size / 2; offset < size && i < PX4_GUID_BYTE_LENGTH; i++) {
		offset += snprintf(&format_buffer[offset], size - offset, "%02x", px4_guid[i]);
	}

	return offset;
}
