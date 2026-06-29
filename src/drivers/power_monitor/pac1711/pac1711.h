#pragma once

#include <stdint.h>

/* PAC1711 I2C Address */
#define PAC1711_BASEADDR              0x41

/* PAC1711 Register Map (from MikroElektronika SDK / DS20005573) */
#define PAC1711_REG_REFRESH           0x00
#define PAC1711_REG_CTRL              0x01
#define PAC1711_REG_ACC_COUNT         0x02
#define PAC1711_REG_VBUS              0x04
#define PAC1711_REG_VSENSE            0x05
#define PAC1711_REG_VBUS_AVG          0x06
#define PAC1711_REG_VSENSE_AVG        0x07
#define PAC1711_REG_VPOWER            0x08
#define PAC1711_REG_PRODUCT_ID        0xFD
#define PAC1711_REG_MFG_ID            0xFE

/* Conversion constants
 * Vsource (VBUS): 16-bit unsigned, FSR = 42V, LSB = 42.0/65536 = 640.9 uV
 * Vsense: 16-bit signed, FSR = 100mV, LSB = 0.1/65536 = 1.526 uV
 * Shunt: 0.3 mOhm (R11 on EV79R88A, rated 150A)
 * Current = Vsense / Rshunt
 */
#define PAC1711_VBUS_FSR_V            42.0f
#define PAC1711_VSENSE_FSR_V          0.1f
#define PAC1711_SHUNT_OHMS            0.0003f
