/****************************************************************************
 * SAMD21 WDT driver for PX4IO.
 *
 * SAMD21 WDT: clocked from GCLK_WDT (typically OSCULP32K = 32.768 kHz).
 * Period register values: 0x0=8cy, 0x1=16cy ... 0xB=16384cy (~500ms max).
 * We target ~4 second timeout: 131072 cycles at 32768 Hz = 0x9 (4096cy=~125ms)
 * Use 0xA = 8192 cy @ 32768 Hz = 250 ms, or 0xB = 16384 cy = 500 ms.
 * To get ~4 s use the early-warning + EWCTRL=0xA (8192cy), PER=0xB (16384cy).
 * Simpler: just use PER=0xB (500 ms) since watchdog_pet() is called every loop.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <chip.h>
#include <hardware/samd21_memorymap.h>

/* WDT register offsets (from SAMD21 datasheet section 17) */
#define SAM_WDT_CTRL_OFFSET    0x00
#define SAM_WDT_CONFIG_OFFSET  0x01
#define SAM_WDT_EWCTRL_OFFSET  0x02
#define SAM_WDT_INTENCLR_OFFSET 0x04
#define SAM_WDT_INTENSET_OFFSET 0x05
#define SAM_WDT_INTFLAG_OFFSET 0x06
#define SAM_WDT_STATUS_OFFSET  0x07
#define SAM_WDT_CLEAR_OFFSET   0x08

#define WDT_BASE  SAM_WDT_BASE

#define WDT_CTRL_ENABLE   (1 << 1)
#define WDT_CTRL_WEN      (1 << 2)   /* window mode enable */
#define WDT_STATUS_SYNCBUSY (1 << 7)
#define WDT_CLEAR_KEY     0xA5

/* PER field: timeout period */
#define WDT_CONFIG_PER_16384CY  0x0B  /* ~500 ms at 32.768 kHz */

#define rCTRL    (*(volatile uint8_t  *)(WDT_BASE + SAM_WDT_CTRL_OFFSET))
#define rCONFIG  (*(volatile uint8_t  *)(WDT_BASE + SAM_WDT_CONFIG_OFFSET))
#define rSTATUS  (*(volatile uint8_t  *)(WDT_BASE + SAM_WDT_STATUS_OFFSET))
#define rCLEAR   (*(volatile uint8_t  *)(WDT_BASE + SAM_WDT_CLEAR_OFFSET))

static inline void wdt_sync(void)
{
	while (rSTATUS & WDT_STATUS_SYNCBUSY) {}
}

void watchdog_pet(void)
{
	wdt_sync();
	rCLEAR = WDT_CLEAR_KEY;
}

void watchdog_init(void)
{
	/* GCLK2 must already be connected to WDT by the clock init code.
	 * Set timeout to ~500 ms (PER=0xB). */
	wdt_sync();
	rCONFIG = WDT_CONFIG_PER_16384CY;
	wdt_sync();
	rCTRL = WDT_CTRL_ENABLE;
	wdt_sync();

	watchdog_pet();
}
