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

#include <board_config.h>
#include <px4_arch/micro_hal.h>
#include <px4_arch/spi_hw_description.h>
#include <px4_platform_common/spi.h>
#include <drivers/drv_sensor.h>
#include <nuttx/spi/spi.h>
#include <px4_platform_common/log.h>

static constexpr px4_spi_bus_device_t make_spidev(uint32_t drvtype, uint32_t cs_gpio,
		spi_drdy_gpio_t drdy_gpio = 0)
{
	return px4_spi_bus_device_t {
		.cs_gpio = cs_gpio,
		.drdy_gpio = drdy_gpio,
		.devid = PX4_SPIDEV_ID(PX4_SPI_DEVICE_ID, drvtype),
		.devtype_driver = static_cast<uint16_t>(drvtype),
	};
}

constexpr px4_spi_bus_t px4_spi_buses[SPI_BUS_MAX_BUS_ITEMS] = {
	{
		.devices = {
			make_spidev(DRV_IMU_DEVTYPE_ICM45686, GPIO_SPI0_CS_ICM45686, GPIO_SPI0_DRDY_ICM45686),
		},
		.power_enable_gpio = 0,
		.bus = static_cast<int8_t>(SPI::Bus::SPI0),
		.is_external = false,
		.requires_locking = true,
	},
};

/* SPI chip select and status functions required by NuttX SAMV7 */
extern "C" {

static void sam_spixselect(SPI::Bus bus_id, uint32_t devid, bool selected)
{
	for (const auto &bus : px4_spi_buses) {
		if (bus.bus != static_cast<int8_t>(bus_id)) {
			continue;
		}

		for (const auto &device : bus.devices) {
			if (device.cs_gpio == 0) {
				continue;
			}

			// Only toggle CS for the matching device to avoid overwriting
			// the CS state when multiple devices share the same CS pin
			if (device.devid == devid) {
				px4_arch_gpiowrite(device.cs_gpio, !selected);
				return;  // Found our device, done
			}
		}
	}
}

void sam_spi0select(uint32_t devid, bool selected)
{
	sam_spixselect(SPI::Bus::SPI0, devid, selected);
}

uint8_t sam_spi0status(struct spi_dev_s *dev, uint32_t devid)
{
	(void)dev;
	(void)devid;
	return SPI_STATUS_PRESENT;
}

void sam_spi1select(uint32_t devid, bool selected)
{
	/* SPI1 unconnected */
	(void)devid;
	(void)selected;
}

uint8_t sam_spi1status(struct spi_dev_s *dev, uint32_t devid)
{
	(void)dev;
	(void)devid;
	return SPI_STATUS_PRESENT;
}

} // extern "C"
