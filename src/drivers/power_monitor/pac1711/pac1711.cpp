#include "pac1711.h"
#include <nuttx/i2c/i2c_master.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/module.h>
#include "sam_twihs.h"
#include <uORB/Publication.hpp>
#include <uORB/topics/battery_status.h>
#include <drivers/drv_hrt.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

static struct i2c_master_s *g_pac1711_i2c = nullptr;
static bool g_pac1711_running = false;
static px4::atomic_bool g_pac1711_should_stop{false};
static float g_voltage = 0.0f;
static float g_current = 0.0f;

static int pac1711_i2c_read_reg16(struct i2c_master_s *i2c, uint8_t addr, uint8_t reg, uint16_t *val)
{
	struct i2c_msg_s msgs[2];
	uint8_t buf[2] = {};

	msgs[0].frequency = 100000;
	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].buffer = &reg;
	msgs[0].length = 1;

	msgs[1].frequency = 100000;
	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_READ;
	msgs[1].buffer = buf;
	msgs[1].length = 2;

	int ret = I2C_TRANSFER(i2c, msgs, 2);
	if (ret == OK) {
		*val = (uint16_t)(buf[0] << 8) | buf[1];
	}
	return ret;
}

static int pac1711_i2c_send_byte(struct i2c_master_s *i2c, uint8_t addr, uint8_t reg)
{
	struct i2c_msg_s msg;

	msg.frequency = 100000;
	msg.addr = addr;
	msg.flags = 0;
	msg.buffer = &reg;
	msg.length = 1;

	return I2C_TRANSFER(i2c, &msg, 1);
}

static int pac1711_thread_main(int argc, char *argv[])
{
	PX4_INFO("pac1711: thread started");

	g_pac1711_i2c = px4_i2cbus_initialize(2);

	if (g_pac1711_i2c == nullptr) {
		PX4_WARN("pac1711: px4_i2cbus_initialize(2) failed, trying sam_i2cbus_initialize(2)");
		g_pac1711_i2c = sam_i2cbus_initialize(2);
	}

	if (g_pac1711_i2c == nullptr) {
		PX4_ERR("pac1711: failed to open I2C bus");
		g_pac1711_running = false;
		return -1;
	}

	PX4_INFO("pac1711: I2C bus opened");

	/* Send REFRESH command (write to reg 0x00) to latch new readings */
	pac1711_i2c_send_byte(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_REFRESH);
	px4_usleep(100000);

	/* Verify chip */
	uint16_t product_id = 0, mfg_id = 0;
	pac1711_i2c_read_reg16(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_PRODUCT_ID, &product_id);
	pac1711_i2c_read_reg16(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_MFG_ID, &mfg_id);
	PX4_INFO("pac1711: product=0x%02x mfg=0x%02x", product_id >> 8, mfg_id >> 8);

	/* Initial read */
	uint16_t vbus_raw = 0;
	pac1711_i2c_read_reg16(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_VBUS, &vbus_raw);
	float v = (float)vbus_raw / 65536.0f * PAC1711_VBUS_FSR_V;
	PX4_INFO("pac1711: VBUS=0x%04x (%.2fV)", vbus_raw, (double)v);

	/* Main loop */
	uORB::Publication<battery_status_s> battery_pub{ORB_ID(battery_status)};

	int loop_count = 0;
	while (!g_pac1711_should_stop.load()) {
		/* REFRESH command latches new conversion results */
		pac1711_i2c_send_byte(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_REFRESH);
		px4_usleep(2000);  /* 2ms for conversion */

		uint16_t vbus = 0, vsense = 0;
		pac1711_i2c_read_reg16(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_VBUS, &vbus);
		pac1711_i2c_read_reg16(g_pac1711_i2c, PAC1711_BASEADDR, PAC1711_REG_VSENSE, &vsense);

		float v_raw = (float)vbus / 65536.0f * PAC1711_VBUS_FSR_V - 0.02f;
		float vsense_v = (float)(int16_t)vsense / 65536.0f * PAC1711_VSENSE_FSR_V;
		float i_raw = vsense_v / PAC1711_SHUNT_OHMS;
		if (i_raw < 0.0f) { i_raw = 0.0f; }

		/* Low-pass filter (alpha=0.2) to smooth noise at low currents */
		g_voltage = g_voltage * 0.8f + v_raw * 0.2f;
		g_current = g_current * 0.8f + i_raw * 0.2f;

		if (loop_count % 50 == 0) {
			PX4_INFO("pac1711: %.2fV %.2fA (raw: vbus=0x%04x vsense=0x%04x)",
				(double)g_voltage, (double)g_current, vbus, vsense);
		}
		loop_count++;

		battery_status_s bat{};
		bat.timestamp = hrt_absolute_time();
		bat.voltage_v = g_voltage;
		bat.current_a = g_current;
		bat.connected = true;
		bat.source = battery_status_s::SOURCE_POWER_MODULE;
		battery_pub.publish(bat);

		px4_usleep(100000);  /* 10 Hz */
	}

	px4_i2cbus_uninitialize(g_pac1711_i2c);
	g_pac1711_i2c = nullptr;
	g_pac1711_running = false;
	PX4_INFO("pac1711: stopped");
	return 0;
}

static void print_usage()
{
	PX4_INFO("Usage: pac1711 {start|stop|status}");
}

extern "C" __EXPORT int pac1711_main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage();
		return -1;
	}

	if (!strcmp(argv[1], "start")) {
		if (g_pac1711_running) {
			PX4_WARN("pac1711: already running");
			return 0;
		}
		g_pac1711_running = true;
		g_pac1711_should_stop.store(false);

		int task = px4_task_spawn_cmd("pac1711",
					     SCHED_DEFAULT,
					     SCHED_PRIORITY_DEFAULT,
					     2048,
					     pac1711_thread_main,
					     nullptr);
		if (task < 0) {
			PX4_ERR("pac1711: task spawn failed");
			g_pac1711_running = false;
			return -1;
		}
		return 0;
	}

	if (!strcmp(argv[1], "stop")) {
		if (!g_pac1711_running) {
			PX4_WARN("pac1711: not running");
			return 0;
		}
		g_pac1711_should_stop.store(true);
		px4_usleep(200000);
		return 0;
	}

	if (!strcmp(argv[1], "status")) {
		if (g_pac1711_running) {
			PX4_INFO("pac1711: running");
			PX4_INFO("  voltage: %.3f V", (double)g_voltage);
			PX4_INFO("  current: %.3f A", (double)g_current);
		} else {
			PX4_INFO("pac1711: not running");
		}
		return 0;
	}

	print_usage();
	return -1;
}
