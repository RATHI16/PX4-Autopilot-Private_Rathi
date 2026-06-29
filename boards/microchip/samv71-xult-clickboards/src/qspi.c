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
 * @file qspi.c
 *
 * Board-level QSPI flash initialization for SAMV71-XULT.
 *
 * Uses SAMV7 QSPI in SPI compatibility mode (CONFIG_SAMV7_QSPI_SPI_MODE)
 * which wraps the QSPI hardware as a standard SPI device for the NuttX
 * W25 MTD driver.
 *
 * Data flow (init):
 *   sam_qspi_spi_initialize(0) -> struct spi_dev_s*
 *   w25_initialize(spi)        -> struct mtd_dev_s*
 *   register_mtddriver("/dev/mtdqspi") -> visible in NuttX VFS
 *
 * Data flow (partitions):
 *   mtd_partition(mtd, offset, nblocks) -> sub-region MTD
 *   ftl_initialize(minor, part_mtd)     -> /dev/mtdblockN
 *   bchdev_register(blk, char, false)   -> /fs/mtd_params, /fs/mtd_waypoints
 *
 * Custom FC board has SST26VF032BA (Microchip, JEDEC bf 26 42, 4MB).
 * Uses NuttX SST26 MTD driver via SPI compatibility mode.
 */

#include <nuttx/config.h>

#ifdef CONFIG_SAMV7_QSPI_SPI_MODE

#include <stdio.h>
#include <errno.h>

#include <nuttx/spi/spi.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/drivers/drivers.h>

#include "sam_qspi_spi.h"
#include "board_config.h"

#include <px4_platform_common/px4_mtd.h>
#include <px4_platform_common/px4_manifest.h>
#include <px4_platform_common/mtd_manifest.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QSPI_FLASH_DEVPATH  "/dev/mtdqspi"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct mtd_dev_s *g_qspi_mtd;

/****************************************************************************
 * Public Functions -- Accessors
 ****************************************************************************/

struct mtd_dev_s *board_get_qspi_mtd(void)
{
	return g_qspi_mtd;
}

/****************************************************************************
 * Public Functions -- Board callbacks required by sam_qspi_spi.c
 ****************************************************************************/

/****************************************************************************
 * Name: sam_qspi_select
 *
 * Description:
 *   Board-specific chip select for QSPI SPI mode. On SAMV71-XULT, CS is
 *   hardware-managed on PA11 -- this is a required stub.
 *
 ****************************************************************************/

void sam_qspi_select(uint32_t devid, bool selected)
{
	/* CS is hardware-managed by the QSPI peripheral on PA11.
	 * With CSMODE_LASTXFER, CS asserts on TDR write and deasserts
	 * when LASTXFER is set on the last byte of an exchange.
	 */
	(void)devid;
	(void)selected;
}

/****************************************************************************
 * Name: sam_qspi_status
 *
 * Description:
 *   Return SPI device status. The flash is soldered on the board,
 *   so it is always present.
 *
 ****************************************************************************/

uint8_t sam_qspi_status(struct spi_dev_s *dev, uint32_t devid)
{
	(void)dev;
	(void)devid;
	return SPI_STATUS_PRESENT;
}

/****************************************************************************
 * Name: board_qspi_flash_init
 *
 * Description:
 *   Initialize the onboard QSPI flash via SPI compatibility mode and
 *   register it as /dev/mtdqspi.
 *
 *   Called from board_app_initialize() in init.c. Non-fatal on failure
 *   (board continues booting with SD card storage only).
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int board_qspi_flash_init(void)
{
	struct spi_dev_s *spi;
	struct mtd_dev_s *mtd;
	struct mtd_geometry_s geo;
	int ret;

	printf("[boot] QSPI flash init: SST26VF032BA via SPI mode\n");

	/* Step 1: Initialize QSPI peripheral in SPI compatibility mode */

	spi = sam_qspi_spi_initialize(0);

	if (spi == NULL) {
		printf("[boot] QSPI SPI init failed\n");
		return -ENODEV;
	}

	/* Step 2: JEDEC ID probe to verify SPI communication.
	 * Expected: S25FL116K -> 01 40 15
	 *           0xFF/0xFF/0xFF = no response (CS, clock, or wiring issue)
	 *           0x00/0x00/0x00 = bus stuck low
	 */
	{
		uint8_t jedec[3];

		SPI_LOCK(spi, true);
		SPI_SETFREQUENCY(spi, 1000000);  /* 1MHz for safe probing */
		SPI_SETMODE(spi, SPIDEV_MODE0);
		SPI_SETBITS(spi, 8);
		SPI_SELECT(spi, SPIDEV_FLASH(0), true);
		SPI_SEND(spi, 0x9f);  /* RDID command */
		jedec[0] = (uint8_t)SPI_SEND(spi, 0xff);
		jedec[1] = (uint8_t)SPI_SEND(spi, 0xff);
		jedec[2] = (uint8_t)SPI_SEND(spi, 0xff);
		SPI_SELECT(spi, SPIDEV_FLASH(0), false);
		SPI_LOCK(spi, false);

		printf("[boot] QSPI JEDEC probe: %02x %02x %02x\n",
		       jedec[0], jedec[1], jedec[2]);

		if ((jedec[0] == 0xff && jedec[1] == 0xff && jedec[2] == 0xff) ||
		    (jedec[0] == 0x00 && jedec[1] == 0x00 && jedec[2] == 0x00)) {
			printf("[boot] QSPI flash not responding (bad JEDEC ID)\n");
			return -ENODEV;
		}
	}

	/* Step 3: Initialize SST26 MTD driver.
	 * Handles SST26VF032BA (Microchip, JEDEC bf 26 42, 4MB).
	 * The driver issues Global Block Protection Unlock automatically.
	 */

	mtd = sst26_initialize_spi(spi, (uint16_t)0);

	if (mtd == NULL) {
		printf("[boot] SST26 init failed (JEDEC mismatch or SPI error)\n");
		return -ENODEV;
	}

	g_qspi_mtd = mtd;

	/* Step 4: Query geometry to log flash size */

	ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));

	if (ret < 0) {
		printf("[boot] QSPI flash MTDIOC_GEOMETRY failed: %d\n", ret);

	} else {
		printf("[boot] QSPI flash: %lu KB (%lu sectors of %lu bytes)\n",
		       (unsigned long)(geo.neraseblocks * geo.erasesize / 1024),
		       (unsigned long)geo.neraseblocks,
		       (unsigned long)geo.erasesize);
	}

	/* Step 5: Register MTD device */

	ret = register_mtddriver(QSPI_FLASH_DEVPATH, mtd, 0755, NULL);

	if (ret < 0) {
		printf("[boot] register_mtddriver(%s) failed: %d\n",
		       QSPI_FLASH_DEVPATH, ret);
		return ret;
	}

	printf("[boot] QSPI flash registered at %s\n", QSPI_FLASH_DEVPATH);
	return OK;
}

/****************************************************************************
 * Name: board_qspi_create_partitions
 *
 * Description:
 *   Create MTD partitions on the QSPI flash for parameter and dataman
 *   storage.  Each partition is exposed as a character device via
 *   mtd_partition() -> ftl_initialize() -> bchdev_register().
 *
 *   Partition layout is defined in board_config.h in erase-sector units.
 *   This function queries the MTD geometry at runtime and converts to
 *   MTD block units (geo.blocksize) for mtd_partition().
 *
 *   Non-fatal: errors are logged but boot continues with SD-only storage.
 *
 * Input Parameters:
 *   mtd - The parent MTD device (from w25_initialize)
 *
 * Returned Value:
 *   OK on success (all partitions created), negative errno on first failure.
 *
 ****************************************************************************/

int board_qspi_create_partitions(struct mtd_dev_s *mtd)
{
	struct mtd_geometry_s geo;
	int ret;

	ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));

	if (ret < 0) {
		printf("[boot] QSPI partition: geometry query failed: %d\n", ret);
		return ret;
	}

	/* Convert erase-sector counts to MTD block counts.
	 * W25/S25FL: blocksize=256, erasesize=4096 -> blkpererase=16
	 */
	int blkpererase = geo.erasesize / geo.blocksize;

	printf("[boot] QSPI partition: blksize=%lu erase=%lu blk/erase=%d\n",
	       (unsigned long)geo.blocksize, (unsigned long)geo.erasesize,
	       blkpererase);

	/* Partition table: { name, path, offset_esect, size_esect, ftl_minor, mtd_type }
	 * mtd_type is used by px4_mtd_query() to satisfy rcS "mft query" commands.
	 */
	static const struct {
		const char *name;
		const char *path;
		int offset_esect;
		int size_esect;
		int ftl_minor;
		int mtd_type;
	} parts[] = {
		{ "params",    "/fs/mtd_params",    QSPI_PART_PARAMS_OFFSET,
		  QSPI_PART_PARAMS_SECTORS,    0, MTD_PARAMETERS },
		{ "caldata",   "/fs/mtd_caldata",   QSPI_PART_CALDATA_OFFSET,
		  QSPI_PART_CALDATA_SECTORS,   1, MTD_CALDATA },
		{ "waypoints", "/fs/mtd_waypoints", QSPI_PART_WAYPOINTS_OFFSET,
		  QSPI_PART_WAYPOINTS_SECTORS, 2, MTD_WAYPOINTS },
	};

	const unsigned nparts = sizeof(parts) / sizeof(parts[0]);

	/* Create each partition: mtd_partition -> ftl -> bchdev */

	for (unsigned i = 0; i < nparts; i++) {
		off_t first_block = (off_t)parts[i].offset_esect * blkpererase;
		off_t num_blocks  = (off_t)parts[i].size_esect * blkpererase;

		struct mtd_dev_s *part = mtd_partition(mtd, first_block, num_blocks);

		if (part == NULL) {
			printf("[boot] QSPI partition %u (%s): mtd_partition failed\n",
			       i, parts[i].name);
			return -ENOMEM;
		}

		ret = ftl_initialize(parts[i].ftl_minor, part);

		if (ret < 0) {
			printf("[boot] QSPI partition %u (%s): ftl_initialize failed: %d\n",
			       i, parts[i].name, ret);
			return ret;
		}

		char blkdev[20];
		snprintf(blkdev, sizeof(blkdev), "/dev/mtdblock%d", parts[i].ftl_minor);

		ret = bchdev_register(blkdev, parts[i].path, false);

		if (ret < 0) {
			printf("[boot] QSPI partition %u (%s): bchdev_register failed: %d\n",
			       i, parts[i].name, ret);
			return ret;
		}

		printf("[boot] QSPI partition %u: %s (%d KB)\n",
		       i, parts[i].path,
		       (int)(parts[i].size_esect * geo.erasesize / 1024));
	}

	/* Register with px4_mtd so "mft query -k MTD -s MTD_CALDATA ..."
	 * in rcS can find our partitions.  Build a static mtd_instance_s.
	 */
	static mtd_instance_s qspi_instance;
	static int            qspi_block_counts[QSPI_NUM_PARTITIONS];
	static int            qspi_types[QSPI_NUM_PARTITIONS];
	static const char    *qspi_names[QSPI_NUM_PARTITIONS];

	qspi_instance.mtd_dev               = mtd;
	qspi_instance.partition_block_counts = qspi_block_counts;
	qspi_instance.partition_types        = qspi_types;
	qspi_instance.partition_names        = qspi_names;
	qspi_instance.part_dev               = NULL;
	qspi_instance.devid                  = 0;
	qspi_instance.n_partitions_current   = nparts;

	for (unsigned i = 0; i < nparts; i++) {
		qspi_block_counts[i] = parts[i].size_esect * blkpererase;
		qspi_types[i]        = parts[i].mtd_type;
		qspi_names[i]        = parts[i].path;
	}

	ret = px4_mtd_register_instance(&qspi_instance);

	if (ret < 0) {
		printf("[boot] QSPI mtd instance registration failed: %d\n", ret);
		return ret;
	}

	return OK;
}

#endif /* CONFIG_SAMV7_QSPI_SPI_MODE */
