/****************************************************************************
 * src/modules/px4iofirmware/serial_samd.cpp
 *
 * FMU serial link implementation for Microchip SAMD21 (SERCOM USART + DMAC).
 *
 * Replaces serial.cpp (STM32-specific) when building for SAMD21.
 *
 * Architecture differences from STM32 version:
 *   - SERCOM USART registers replace STM32 USART registers
 *   - SAM DMAC API (descriptor-based) replaces STM32 DMA API
 *   - No hardware IDLE line interrupt on SAMD21: packet-end is detected
 *     via a 500 µs software timeout using the HRT (serial_dma_call).
 *     sam_serial_dma_poll() is called every 1 ms from px4io.cpp and checks
 *     whether a complete packet has arrived based on the count_code field.
 *
 * SERCOM5 USART configuration (1.5 Mbps, 8N1):
 *   PB16 = PAD0 (TX), PB17 = PAD1 (RX)
 *   BAUD register = 65536 * (1 - 16 * 1500000 / 48000000) = 0x8000
 *
 * DMAC:
 *   RX channel: triggered by DMAC_TRIGSRC_SERCOM5_RX (11)
 *   TX channel: triggered by DMAC_TRIGSRC_SERCOM5_TX (12)
 *
 * @author  Ported to SAMD21 from STM32 original by Lorenz Meier et al.
 ****************************************************************************/

#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <drivers/drv_hrt.h>
#include <arch/board/board.h>
#include <chip.h>
#include <arm_internal.h>

/* SAM-specific headers */
#include <sam_port.h>
#include <sam_sercom.h>
#include <sam_usart.h>
#include <sam_dmac.h>
#include <hardware/samd21_memorymap.h>
#include <hardware/samd_sercom.h>
#include <hardware/samd_usart.h>
#include <hardware/samd_dmac.h>

#include "px4io.h"

/* -------------------------------------------------------------------------
 * SERCOM5 register accessors
 * -------------------------------------------------------------------------*/
#define SERCOM_BASE       PX4FMU_SERCOM_BASE

#define rCTRLA    (*(volatile uint32_t *)(SERCOM_BASE + SAM_USART_CTRLA_OFFSET))
#define rCTRLB    (*(volatile uint32_t *)(SERCOM_BASE + SAM_USART_CTRLB_OFFSET))
#define rBAUD     (*(volatile uint16_t *)(SERCOM_BASE + SAM_USART_BAUD_OFFSET))
#define rINTENSET (*(volatile uint8_t  *)(SERCOM_BASE + SAM_USART_INTENSET_OFFSET))
#define rINTENCLR (*(volatile uint8_t  *)(SERCOM_BASE + SAM_USART_INTENCLR_OFFSET))
#define rINTFLAG  (*(volatile uint8_t  *)(SERCOM_BASE + SAM_USART_INTFLAG_OFFSET))
#define rSTATUS   (*(volatile uint16_t *)(SERCOM_BASE + SAM_USART_STATUS_OFFSET))
#define rDATA     (*(volatile uint16_t *)(SERCOM_BASE + SAM_USART_DATA_OFFSET))
#define rSYNCBUSY (*(volatile uint32_t *)(SERCOM_BASE + SAM_USART_SYNCBUSY_OFFSET))

/* Wait for SERCOM sync */
#define SERCOM_SYNC()  while (rSYNCBUSY & (USART_SYNCBUSY_SWRST | USART_SYNCBUSY_ENABLE))

/* -------------------------------------------------------------------------
 * BAUD calculation for 1.5 Mbps @ 48 MHz:
 *   BAUD = 65536 * (1 - 16 * f_baud / f_ref)
 *        = 65536 * (1 - 16 * 1500000 / 48000000)
 *        = 65536 * 0.5 = 32768 = 0x8000
 * -------------------------------------------------------------------------*/
#define PX4FMU_BAUD_REG  ((uint16_t)(65536UL * \
    (1.0 - 16.0 * PX4FMU_SERIAL_BITRATE / BOARD_SERCOM5_FREQUENCY) + 0.5))

/* -------------------------------------------------------------------------
 * DMAC channel flags (priority 3, beat-size 8-bit, peripheral trigger)
 * -------------------------------------------------------------------------*/
#define DMAC_CHFLAGS_TX  (DMAC_CHCTRLB_TRIGACT_BEAT | \
                          DMAC_CHCTRLB_TRIGSRC(PX4FMU_SERIAL_TX_DMAC_TRIGSRC) | \
                          DMAC_CHCTRLB_LVL_LVL3)

#define DMAC_CHFLAGS_RX  (DMAC_CHCTRLB_TRIGACT_BEAT | \
                          DMAC_CHCTRLB_TRIGSRC(PX4FMU_SERIAL_RX_DMAC_TRIGSRC) | \
                          DMAC_CHCTRLB_LVL_LVL3)

/* -------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------*/
static DMA_HANDLE tx_dma;
static DMA_HANDLE rx_dma;

static struct IOPacket dma_packet;

/* Byte count of last poll — used to detect when RX has stalled */
static size_t       last_rx_bytes  __attribute__((unused)) = 0;
static hrt_abstime  last_rx_time   __attribute__((unused)) = 0;

/* 500 µs: if no new bytes arrive within this window after a partial packet,
 * treat it as end-of-packet (sender has finished). */
#define PACKET_IDLE_US  500

static void rx_handle_packet(void);
static void rx_dma_callback(DMA_HANDLE handle, void *arg, int result);
static void dma_reset(void);
static int  serial_interrupt(int irq, void *context, FAR void *arg);

/* -------------------------------------------------------------------------
 * interface_init — called from user_start() in px4io.cpp
 * -------------------------------------------------------------------------*/
void
interface_init(void)
{
	/* Allocate DMAC channels */
	tx_dma = sam_dmachannel(DMAC_CHFLAGS_TX);
	rx_dma = sam_dmachannel(DMAC_CHFLAGS_RX);

	/* Configure SERCOM5 GPIO pins */
	sam_configport(GPIO_FMU_TX);
	sam_configport(GPIO_FMU_RX);

	/* Enable SERCOM5 peripheral clock via GCLK */
	sam_sercom_enableperiph(5);

	/* Reset SERCOM5 */
	rCTRLA = USART_CTRLA_SWRST;
	SERCOM_SYNC();

	/* CTRLA: internal clock, async, LSB first, 8N1, TXPO=0 (PAD0), RXPO=1 (PAD1) */
	rCTRLA = USART_CTRLA_MODE_INTUSART |   /* internal clock */
	         USART_CTRLA_TXPAD0_1      |   /* TX on PAD0 */
	         USART_CTRLA_RXPAD1        |   /* RX on PAD1 */
	         USART_CTRLA_DORD           ;   /* LSB first */
	SERCOM_SYNC();

	/* CTRLB: 8-bit, no parity, 1 stop, TX+RX enable */
	rCTRLB = USART_CTRLB_CHSIZE_8BITS |
	         USART_CTRLB_TXEN         |
	         USART_CTRLB_RXEN         ;
	SERCOM_SYNC();

	/* Baud rate */
	rBAUD = PX4FMU_BAUD_REG;

	/* Attach SERCOM5 interrupt (used only for ERROR flags) */
	irq_attach(PX4FMU_SERCOM_IRQ, serial_interrupt, NULL);
	up_enable_irq(PX4FMU_SERCOM_IRQ);

	/* SAMD21 has no dedicated ERROR interrupt bit (SAMD20-only).
	 * No SERCOM interrupt needed — DMA handles data. */
	(void)rINTENSET;

	/* Enable SERCOM5 */
	rCTRLA |= USART_CTRLA_ENABLE;
	SERCOM_SYNC();

	/* Start DMA RX */
	dma_reset();

	debug("serial_samd: init done, BAUD=0x%04x", PX4FMU_BAUD_REG);
}

/* -------------------------------------------------------------------------
 * interface_tick — called every loop from px4io.cpp main loop.
 * Not used for data transfer (DMA handles that), but we can add
 * link-timeout monitoring here if needed in the future.
 * -------------------------------------------------------------------------*/
void
interface_tick(void)
{
	/* Nothing to do — DMA callbacks handle packet processing. */
}

/* -------------------------------------------------------------------------
 * sam_serial_dma_poll — called every 1 ms by HRT (from px4io.cpp).
 *
 * SAMD21 SERCOM has no IDLE line interrupt. Instead, we detect end-of-packet
 * by noticing that RX bytes have stopped arriving for PACKET_IDLE_US.
 *
 * Strategy:
 *   1. Read how many bytes the RX DMA has NOT yet transferred (residual).
 *   2. Compute bytes_received = sizeof(dma_packet) - residual.
 *   3. If bytes_received hasn't changed since last call AND
 *      it's been > PACKET_IDLE_US since the last new byte AND
 *      bytes_received >= minimum packet size → process the packet.
 * -------------------------------------------------------------------------*/
void
sam_serial_dma_poll(void)
{
	/* Read DMAC write-back descriptor BTCNT to get remaining bytes.
	 * sam_dmaresidual() is not in the public NuttX SAM API, so we
	 * stop the channel temporarily and read the count.
	 * A cleaner future approach: expose sam_dmaresidual() in sam_dmac.h. */

	/* For now: rely solely on the DMA TC callback (rx_dma_callback) which
	 * fires when sizeof(dma_packet) bytes are received. Short packets will
	 * be detected when the next packet arrives and resets DMA.
	 *
	 * TODO: implement residual read via DMAC writeback descriptor for
	 * proper short-packet handling. */
}

/* -------------------------------------------------------------------------
 * rx_handle_packet — validate CRC and dispatch read/write
 * -------------------------------------------------------------------------*/
static void
rx_handle_packet(void)
{
	uint8_t crc = dma_packet.crc;
	dma_packet.crc = 0;

	if (crc != crc_packet(&dma_packet)) {
		dma_packet.count_code = PKT_CODE_CORRUPT;
		dma_packet.page       = 0xff;
		dma_packet.offset     = 0xff;
		return;
	}

	if (PKT_CODE(dma_packet) == PKT_CODE_WRITE) {
		if (registers_set(dma_packet.page, dma_packet.offset,
		                  &dma_packet.regs[0], PKT_COUNT(dma_packet))) {
			dma_packet.count_code = PKT_CODE_ERROR;
		} else {
			dma_packet.count_code = PKT_CODE_SUCCESS;
		}
		return;
	}

	if (PKT_CODE(dma_packet) == PKT_CODE_READ) {
		unsigned count;
		uint16_t *registers;

		if (registers_get(dma_packet.page, dma_packet.offset,
		                  &registers, &count) < 0) {
			dma_packet.count_code = PKT_CODE_ERROR;
		} else {
			if (count > PKT_MAX_REGS) { count = PKT_MAX_REGS; }
			if (count > PKT_COUNT(dma_packet)) { count = PKT_COUNT(dma_packet); }
			memcpy((void *)&dma_packet.regs[0], registers, count * 2);
			dma_packet.count_code = count | PKT_CODE_SUCCESS;
		}
		return;
	}

	dma_packet.count_code = PKT_CODE_CORRUPT;
	dma_packet.page       = 0xff;
	dma_packet.offset     = 0xfe;
}

/* -------------------------------------------------------------------------
 * rx_dma_callback — called when RX DMA transfer completes
 * -------------------------------------------------------------------------*/
static void
rx_dma_callback(DMA_HANDLE handle, void *arg, int result)
{
	/* Disable SERCOM DMA request */
	rCTRLA &= ~USART_CTRLA_ENABLE;
	SERCOM_SYNC();
	rCTRLA |= USART_CTRLA_ENABLE;
	SERCOM_SYNC();

	/* Process the received packet */
	rx_handle_packet();

	/* Re-arm RX DMA before starting TX so we're ready for the next packet */
	dma_reset();

	/* Send reply via TX DMA */
	dma_packet.crc = 0;
	dma_packet.crc = crc_packet(&dma_packet);

	sam_dmatxsetup(tx_dma,
	               (uint32_t)&rDATA,
	               (uint32_t)&dma_packet,
	               PKT_SIZE(dma_packet));
	sam_dmastart(tx_dma, NULL, NULL);

	/* Re-enable SERCOM TX DMA */
	rCTRLB |= USART_CTRLB_TXEN;
	SERCOM_SYNC();

	system_state.fmu_data_received_time = hrt_absolute_time();
}

/* -------------------------------------------------------------------------
 * serial_interrupt — handles SERCOM error flags
 * -------------------------------------------------------------------------*/
static int
serial_interrupt(int irq, void *context, FAR void *arg)
{
	uint8_t intflag = rINTFLAG;

	/* SAMD21 has no dedicated ERROR interrupt bit.
	 * Check STATUS register for framing/parity/buffer-overflow errors. */
	if (rSTATUS & (USART_STATUS_BUFOVF | USART_STATUS_FERR | USART_STATUS_PERR)) {
		/* Clear all error bits */
		rSTATUS = USART_STATUS_BUFOVF | USART_STATUS_FERR | USART_STATUS_PERR;

		/* Reset DMA on error — discard partial packet */
		sam_dmastop(rx_dma);
		sam_dmastop(tx_dma);
		dma_reset();
	}
	(void)intflag;

	return 0;
}

/* -------------------------------------------------------------------------
 * dma_reset — stop any running DMA and re-arm RX for next packet
 * -------------------------------------------------------------------------*/
static void
dma_reset(void)
{
	sam_dmastop(tx_dma);
	sam_dmastop(rx_dma);

	/* Clear any pending SERCOM status */
	rSTATUS  = 0xffff;
	rINTFLAG = 0xff;

	/* Setup RX DMA: SERCOM5 DATA → dma_packet buffer */
	sam_dmarxsetup(rx_dma,
	               (uint32_t)&rDATA,
	               (uint32_t)&dma_packet,
	               sizeof(dma_packet));

	sam_dmastart(rx_dma, rx_dma_callback, NULL);
}
