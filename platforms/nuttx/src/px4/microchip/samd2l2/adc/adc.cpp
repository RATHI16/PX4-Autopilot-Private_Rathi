/****************************************************************************
 * SAMD21 ADC driver for PX4IO (VSERVO + RSSI).
 *
 * Uses ADC0 with AIN[0] (PA2 = VSERVO) and AIN[1] (PA3 = RSSI).
 * Single-ended, 12-bit, internal VDDANA/2 reference (≈1.65V),
 * or INTVCC1 (VDDANA) depending on signal range. We use INTVCC1 (=VDD).
 *
 * SAMD21 ADC register offsets (from datasheet section 33).
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <stdint.h>
#include <nuttx/arch.h>
#include <drivers/drv_hrt.h>
#include <chip.h>
#include <arm_internal.h>
#include <hardware/samd21_memorymap.h>

/* ADC register offsets */
#define ADC_CTRLA_OFFSET      0x00
#define ADC_REFCTRL_OFFSET    0x01
#define ADC_AVGCTRL_OFFSET    0x02
#define ADC_SAMPCTRL_OFFSET   0x03
#define ADC_CTRLB_OFFSET      0x04
#define ADC_WINCTRL_OFFSET    0x08
#define ADC_SWTRIG_OFFSET     0x0C
#define ADC_INPUTCTRL_OFFSET  0x10
#define ADC_EVCTRL_OFFSET     0x14
#define ADC_INTENCLR_OFFSET   0x16
#define ADC_INTENSET_OFFSET   0x17
#define ADC_INTFLAG_OFFSET    0x18
#define ADC_STATUS_OFFSET     0x19
#define ADC_RESULT_OFFSET     0x1A
#define ADC_CALIB_OFFSET      0x28

#define ADC_BASE  SAM_ADC_BASE

#define rCTRLA    (*(volatile uint8_t  *)(ADC_BASE + ADC_CTRLA_OFFSET))
#define rREFCTRL  (*(volatile uint8_t  *)(ADC_BASE + ADC_REFCTRL_OFFSET))
#define rCTRLB    (*(volatile uint16_t *)(ADC_BASE + ADC_CTRLB_OFFSET))
#define rSWTRIG   (*(volatile uint8_t  *)(ADC_BASE + ADC_SWTRIG_OFFSET))
#define rINPUT    (*(volatile uint32_t *)(ADC_BASE + ADC_INPUTCTRL_OFFSET))
#define rINTFLAG  (*(volatile uint8_t  *)(ADC_BASE + ADC_INTFLAG_OFFSET))
#define rSTATUS   (*(volatile uint8_t  *)(ADC_BASE + ADC_STATUS_OFFSET))
#define rRESULT   (*(volatile uint16_t *)(ADC_BASE + ADC_RESULT_OFFSET))
#define rCALIB    (*(volatile uint16_t *)(ADC_BASE + ADC_CALIB_OFFSET))

/* CTRLA */
#define ADC_CTRLA_SWRST   (1 << 0)
#define ADC_CTRLA_ENABLE  (1 << 1)

/* REFCTRL: INTVCC1 = VDDANA (full rail, unbuffered) */
#define ADC_REF_INTVCC1   0x03

/* CTRLB: 12-bit, single-ended, free-run off */
#define ADC_CTRLB_RESSEL_12BIT  (0x0 << 0)
#define ADC_CTRLB_PRESCALER_DIV512 (0x9 << 8)

/* STATUS */
#define ADC_STATUS_SYNCBUSY (1 << 7)

/* INTFLAG */
#define ADC_INTFLAG_RESRDY  (1 << 0)

/* INPUTCTRL: MUXNEG = GND (0x18) */
#define ADC_MUXNEG_GND  0x18

#define ADC_SYNC()  while (rSTATUS & ADC_STATUS_SYNCBUSY) {}

extern "C" {

/* up_udelay is defined in NuttX libarch.a, but link ordering places libarch.a
 * before librc.a (which references up_udelay via dsm.cpp).  Providing a weak
 * definition here ensures the symbol is resolved after librc is processed.
 * The strong definition in libarch.a takes precedence if it happens to be
 * extracted first; otherwise this implementation serves as the fallback. */
void __attribute__((weak)) up_udelay(uint32_t usec)
{
	hrt_abstime t0 = hrt_absolute_time();

	while (hrt_elapsed_time(&t0) < (hrt_abstime)usec) {}
}

int
adc_init(void)
{
	/* Enable ADC peripheral clock */
	uint32_t apbcmask = getreg32(SAM_PM_BASE + 0x20);
	apbcmask |= (1 << 16);  /* PM_APBCMASK_ADC */
	putreg32(apbcmask, SAM_PM_BASE + 0x20);

	/* Connect GCLK0 to ADC */
	putreg16((1 << 14) | (0 << 8) | 0x1E /* GCLK_ADC */, SAM_GCLK_BASE + 0x02);

	/* Reset ADC */
	rCTRLA = ADC_CTRLA_SWRST;
	ADC_SYNC();

	/* Load NVM calibration (linearity + bias) */
	uint32_t calib = getreg32(SAM_NVMCALIB_AREA + 0x18);
	uint16_t linearity = (calib >> 27) & 0x1F;
	uint16_t bias      = (calib >> 3)  & 0x07;
	rCALIB = (bias << 8) | linearity;

	/* Reference: INTVCC1 (VDDANA) */
	rREFCTRL = ADC_REF_INTVCC1;
	ADC_SYNC();

	/* 12-bit, prescaler /512 (~93.75 kHz ADC clock at 48 MHz) */
	rCTRLB = ADC_CTRLB_RESSEL_12BIT | ADC_CTRLB_PRESCALER_DIV512;
	ADC_SYNC();

	/* Enable ADC */
	rCTRLA |= ADC_CTRLA_ENABLE;
	ADC_SYNC();

	return 0;
}

uint16_t
adc_measure(unsigned channel)
{
	/* Set MUXPOS = channel, MUXNEG = GND */
	ADC_SYNC();
	rINPUT = ((channel & 0x1F) << 0) | (ADC_MUXNEG_GND << 8);
	ADC_SYNC();

	/* Software trigger */
	rSWTRIG = (1 << 1);  /* START */
	ADC_SYNC();

	/* Wait for result ready (timeout 200 µs) */
	hrt_abstime t0 = hrt_absolute_time();
	while (!(rINTFLAG & ADC_INTFLAG_RESRDY)) {
		if (hrt_elapsed_time(&t0) > 200) {
			return 0xffff;
		}
	}

	uint16_t result = rRESULT;
	rINTFLAG = ADC_INTFLAG_RESRDY;  /* clear */
	return result;
}

} /* extern "C" */
