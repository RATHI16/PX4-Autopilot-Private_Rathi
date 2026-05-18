/****************************************************************************
 * SAMD21 High-Resolution Timer (HRT) using TC4+TC5 as a 32-bit counter.
 *
 * TC4 and TC5 are chained in COUNT32 mode.
 * Clock: GCLK0 = 48 MHz → prescaler /1 → 48 MHz tick → 20.83 ns resolution.
 * hrt_absolute_time() returns µs: ticks / 48.
 *
 * CC0 is used for callout scheduling (compare match interrupt MC0).
 *
 * NOTE: TC3 may be used by NuttX system tick; we use TC4+TC5 to avoid conflict.
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <sys/types.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <time.h>
#include <queue.h>
#include <errno.h>
#include <string.h>

#include <board_config.h>
#include <drivers/drv_hrt.h>
#include <chip.h>
#include <arm_internal.h>

#ifdef CONFIG_DEBUG_HRT
#  define hrtinfo _info
#else
#  define hrtinfo(x...)
#endif

/* --------------------------------------------------------------------------
 * TC4 register map (COUNT32 mode)
 * -------------------------------------------------------------------------- */
#define HRT_TC_BASE       SAM_TC4_BASE

#define TC_CTRLA_OFF      0x00   /* uint16 */
#define TC_READREQ_OFF    0x02   /* uint16 */
#define TC_CTRLBCLR_OFF   0x04   /* uint8  */
#define TC_CTRLBSET_OFF   0x05   /* uint8  */
#define TC_INTENCLR_OFF   0x0C   /* uint8  */
#define TC_INTENSET_OFF   0x0D   /* uint8  */
#define TC_INTFLAG_OFF    0x0E   /* uint8  */
#define TC_STATUS_OFF     0x0F   /* uint8  */
#define TC_COUNT32_OFF    0x10   /* uint32 */
#define TC_CC0_OFF        0x18   /* uint32 */

#define TC_CTRLA_SWRST         (1u << 0)
#define TC_CTRLA_ENABLE        (1u << 1)
#define TC_CTRLA_MODE_COUNT32  (2u << 2)
#define TC_CTRLA_PRESCALER_1   (0u << 8)

#define TC_INTFLAG_MC0         (1u << 4)
#define TC_STATUS_SYNCBUSY     (1u << 7)

#define rCTRLA    (*(volatile uint16_t *)(HRT_TC_BASE + TC_CTRLA_OFF))
#define rREADREQ  (*(volatile uint16_t *)(HRT_TC_BASE + TC_READREQ_OFF))
#define rINTENSET (*(volatile uint8_t  *)(HRT_TC_BASE + TC_INTENSET_OFF))
#define rINTENCLR (*(volatile uint8_t  *)(HRT_TC_BASE + TC_INTENCLR_OFF))
#define rINTFLAG  (*(volatile uint8_t  *)(HRT_TC_BASE + TC_INTFLAG_OFF))
#define rSTATUS   (*(volatile uint8_t  *)(HRT_TC_BASE + TC_STATUS_OFF))
#define rCOUNT    (*(volatile uint32_t *)(HRT_TC_BASE + TC_COUNT32_OFF))
#define rCC0      (*(volatile uint32_t *)(HRT_TC_BASE + TC_CC0_OFF))

#define TC_SYNC()  do { while (rSTATUS & TC_STATUS_SYNCBUSY) {} } while(0)

/* IRQ number for TC4 */
#define HRT_IRQ    SAM_IRQ_TC4

/* 48 MHz → 1 µs = 48 ticks */
#define HRT_TICKS_PER_US   48u

/* Callout scheduling constants (in µs) */
#define HRT_INTERVAL_MIN   50u
#define HRT_INTERVAL_MAX   4000000000ULL

/* --------------------------------------------------------------------------
 * Call queue and latency tracking
 * -------------------------------------------------------------------------- */
static struct sq_queue_s callout_queue;

static hrt_abstime latency_baseline;
static hrt_abstime latency_actual;

const uint16_t latency_bucket_count = LATENCY_BUCKET_COUNT;
const uint16_t latency_buckets[LATENCY_BUCKET_COUNT] = { 1, 2, 5, 10, 20, 50, 100, 1000 };
__EXPORT uint32_t latency_counters[LATENCY_BUCKET_COUNT + 1];

/* PPM globals — defined here so the RC library and controls.cpp can link them.
 * SAMD21 does not decode PPM in hardware; PPM input is handled by the SW
 * decoder if PPM is wired up. */
#define PPM_MAX_CHANNELS  12
__EXPORT uint16_t ppm_buffer[PPM_MAX_CHANNELS];
__EXPORT uint16_t ppm_frame_length = 0;
__EXPORT unsigned ppm_decoded_channels = 0;
__EXPORT uint64_t ppm_last_valid_decode = 0;

/* Cast helpers: via void* to suppress -Wcast-align (sq_entry_t is the
 * first member of hrt_call, so alignment is always correct). */
#define HRT_PEEK(q)    ((struct hrt_call *)(void *)sq_peek(q))
#define HRT_NEXT(e)    ((struct hrt_call *)(void *)sq_next(&(e)->link))

/* Forward declarations (all internal statics) */
static int  hrt_isr(int irq, void *context, void *arg);
static void hrt_call_internal(struct hrt_call *entry, hrt_abstime deadline,
                               hrt_abstime interval, hrt_callout callout, void *arg);
static void hrt_call_enter(struct hrt_call *entry);
static void hrt_call_reschedule(void);
static void hrt_call_invoke(void);
static void hrt_latency_update(void);

/* --------------------------------------------------------------------------
 * hrt_absolute_time — return current time in microseconds.
 * -------------------------------------------------------------------------- */
hrt_abstime
hrt_absolute_time(void)
{
	/* Request synchronised read of COUNT */
	rREADREQ = (1u << 15) | (TC_COUNT32_OFF & 0x1fu);
	TC_SYNC();
	uint32_t ticks = rCOUNT;
	return (hrt_abstime)(ticks / HRT_TICKS_PER_US);
}

/* --------------------------------------------------------------------------
 * hrt_init — configure TC4+TC5 as a 32-bit free-running counter at 48 MHz.
 * -------------------------------------------------------------------------- */
void
hrt_init(void)
{
	sq_init(&callout_queue);

	/* Enable TC4 and TC5 peripheral clocks via PM_APBCMASK */
	uint32_t apbcmask = getreg32(SAM_PM_BASE + 0x20u);
	apbcmask |= (1u << 12) | (1u << 13);   /* TC4=bit12, TC5=bit13 */
	putreg32(apbcmask, SAM_PM_BASE + 0x20u);

	/* Connect GCLK0 to TC4+TC5 (GCLK_CLKCTRL: GEN=0, ID=0x1C, CLKEN=1) */
	putreg16((uint16_t)((1u << 14) | (0u << 8) | 0x1Cu),
	         SAM_GCLK_BASE + 0x02u);

	/* Reset TC4 */
	rCTRLA = TC_CTRLA_SWRST;
	TC_SYNC();

	/* Configure as COUNT32 master, prescaler /1 */
	rCTRLA = TC_CTRLA_MODE_COUNT32 | TC_CTRLA_PRESCALER_1;
	TC_SYNC();

	/* Set CC0 far in the future so we don't get a spurious interrupt */
	rCC0 = 0xffffffffu;
	TC_SYNC();

	/* Attach ISR */
	irq_attach(HRT_IRQ, hrt_isr, NULL);

	/* Enable TC4 */
	rCTRLA |= TC_CTRLA_ENABLE;
	TC_SYNC();

	/* Enable MC0 compare-match interrupt */
	rINTENSET = TC_INTFLAG_MC0;

	up_enable_irq(HRT_IRQ);
}

/* --------------------------------------------------------------------------
 * hrt_isr — TC4 interrupt service routine.
 * -------------------------------------------------------------------------- */
static int
hrt_isr(int irq, void *context, void *arg)
{
	uint8_t status = rINTFLAG;

	if (status & TC_INTFLAG_MC0) {
		latency_actual = hrt_absolute_time();
		rINTFLAG = TC_INTFLAG_MC0;  /* clear by writing 1 */
		hrt_latency_update();
		hrt_call_invoke();
		hrt_call_reschedule();
	}

	return OK;
}

/* --------------------------------------------------------------------------
 * Public HRT callout API
 * -------------------------------------------------------------------------- */
void
hrt_call_after(struct hrt_call *entry, hrt_abstime delay, hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, hrt_absolute_time() + delay, 0, callout, arg);
}

void
hrt_call_at(struct hrt_call *entry, hrt_abstime calltime, hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, calltime, 0, callout, arg);
}

void
hrt_call_every(struct hrt_call *entry, hrt_abstime delay, hrt_abstime interval,
               hrt_callout callout, void *arg)
{
	hrt_call_internal(entry, hrt_absolute_time() + delay, interval, callout, arg);
}

bool
hrt_called(struct hrt_call *entry)
{
	return (entry->deadline == 0);
}

void
hrt_cancel(struct hrt_call *entry)
{
	irqstate_t flags = px4_enter_critical_section();
	sq_rem(&entry->link, &callout_queue);
	entry->deadline = 0;
	entry->period = 0;
	px4_leave_critical_section(flags);
}

void
hrt_call_init(struct hrt_call *entry)
{
	memset(entry, 0, sizeof(*entry));
}

void
hrt_call_delay(struct hrt_call *entry, hrt_abstime delay)
{
	entry->deadline = hrt_absolute_time() + delay;
}

/* --------------------------------------------------------------------------
 * Internal call queue functions
 * -------------------------------------------------------------------------- */
static void
hrt_call_internal(struct hrt_call *entry, hrt_abstime deadline,
                  hrt_abstime interval, hrt_callout callout, void *arg)
{
	irqstate_t flags = px4_enter_critical_section();

	if (entry->deadline != 0) {
		sq_rem(&entry->link, &callout_queue);
	}

	entry->deadline = deadline;
	entry->period   = interval;
	entry->callout  = callout;
	entry->arg      = arg;

	hrt_call_enter(entry);

	px4_leave_critical_section(flags);
}

static void
hrt_call_enter(struct hrt_call *entry)
{
	struct hrt_call *call, *next;

	call = HRT_PEEK(&callout_queue);

	if ((call == NULL) || (entry->deadline < call->deadline)) {
		sq_addfirst(&entry->link, &callout_queue);
		hrt_call_reschedule();
	} else {
		do {
			next = HRT_NEXT(call);

			if ((next == NULL) || (entry->deadline < next->deadline)) {
				sq_addafter(&call->link, &entry->link, &callout_queue);
				break;
			}
		} while ((call = next) != NULL);
	}
}

static void
hrt_call_invoke(void)
{
	struct hrt_call *call;
	hrt_abstime deadline;

	while (true) {
		hrt_abstime now = hrt_absolute_time();

		call = HRT_PEEK(&callout_queue);

		if (call == NULL || call->deadline > now) {
			break;
		}

		sq_rem(&call->link, &callout_queue);
		deadline = call->deadline;
		call->deadline = 0;

		if (call->callout) {
			call->callout(call->arg);
		}

		if (call->period != 0) {
			if (call->deadline <= now) {
				call->deadline = deadline + call->period;
			}

			hrt_call_enter(call);
		}
	}
}

static void
hrt_call_reschedule(void)
{
	hrt_abstime now  = hrt_absolute_time();
	struct hrt_call *next = HRT_PEEK(&callout_queue);
	hrt_abstime deadline  = now + HRT_INTERVAL_MAX;

	if (next != NULL) {
		if (next->deadline <= (now + HRT_INTERVAL_MIN)) {
			deadline = now + HRT_INTERVAL_MIN;
		} else if (next->deadline < deadline) {
			deadline = next->deadline;
		}
	}

	latency_baseline = deadline;

	/* Convert µs deadline to 48 MHz ticks and load CC0 */
	uint32_t ticks = (uint32_t)(deadline * HRT_TICKS_PER_US);
	TC_SYNC();
	rCC0 = ticks;
	TC_SYNC();
	/* Re-arm MC0 interrupt */
	rINTENSET = TC_INTFLAG_MC0;
}

static void
hrt_latency_update(void)
{
	uint16_t latency = (uint16_t)(latency_actual - latency_baseline);
	unsigned index;

	for (index = 0; index < LATENCY_BUCKET_COUNT; index++) {
		if (latency <= latency_buckets[index]) {
			latency_counters[index]++;
			return;
		}
	}

	latency_counters[index]++;
}
