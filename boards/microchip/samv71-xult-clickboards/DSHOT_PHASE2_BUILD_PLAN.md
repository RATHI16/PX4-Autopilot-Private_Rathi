# Plan: DShot Output via PWMC Sync Mode + XDMAC (Phase 2)

## Context

The SAMV71-XULT board currently has working 4-channel PWMC PWM output (50-400Hz) for
conventional ESCs, verified HITL with jMAVSim, and all sensors/storage working. Phase 2
adds DShot digital protocol output (DShot150/300/600) using the PWMC's Synchronous Channel
Mode with XDMAC DMA, enabling precise motor control without analog PWM jitter.

The existing `DSHOT_IMPLEMENTATION_PLAN.md` contains detailed register-level reference.
This plan is the actionable build sequence.

## Architecture Summary

**STM32 uses TIM DMA burst → SAMV7 uses PWMC Sync Channel Mode (SCM.UPDM=2) + XDMAC.**

- PWMC SCM synchronizes active channels to share the same period
- XDMAC writes duty values to PWMC DMAR register (one word per sync channel per period)
- Each DShot bit = one PWMC period: bit-1 = 75% duty, bit-0 = 37.5% duty
- Clock: MCK/2 = 75 MHz (CPRE=1). DShot600: CPRD=125, DShot150: CPRD=500
- DMA buffer: 17 periods × 4 channels = 68 words per frame (fixed 4-ch layout)
- All 4 PWMC channels synchronized regardless of `channel_mask`; unused channels
  get reset-duty values (output idles). See "Buffer Layout Model" below.
- Buffer in `.nocache` SRAM section; `up_clean_dcache()` called as defense-in-depth

## What Already Exists

- `dshot.h` header with `dshot_conf_t` struct (xdmac_ch_tx, reserved bidir fields)
- `IOTimerChanMode_Dshot` case in `io_timer_channel_init()` — GPIO setup only (correct)
- Two stubs in `io_timer_pwmc.c`: `io_timer_update_dma_req()` and `io_timer_set_dshot_mode()`
- `io_timer_set_enable()` in `io_timer_pwmc.c` — already handles per-channel PWM_ENA/DIS
- `CONFIG_DRIVERS_DSHOT` commented out in `default.px4board`
- No `dshot/` directory or `dshot.c` file yet
- Register defines in `sam_pwm.h`: `SCM_SYNC_*`, `SCM_UPDM_MODE2`, `SCM_PTRM`,
  `SCUC_UPDULOCK`, `IR2_WRDY`, `IR2_UNRE`, `SAMV7_PWM_DMAR`

## Buffer Layout Model: Fixed 4-Channel Order

**Decision: All 4 PWMC hardware channels are always synchronized.**

PWMC sync mode requires synchronized channels to share the same period — you cannot
mix sync and non-sync channels on the same PWMC module. Therefore all 4 channels
(CH0-CH3) are always in the sync group, and the DMA buffer is always 68 words
(17 periods × 4 channels), regardless of how many motors `channel_mask` requests.

```
Per period, 4 words in ascending HW channel order:
  [CH0_duty, CH1_duty, CH2_duty, CH3_duty]

17 periods total:
  Index 0-3:   bit15 duties (CH0, CH1, CH2, CH3)
  Index 4-7:   bit14 duties
  ...
  Index 60-63: bit0 duties
  Index 64-67: reset period (all = dshot_duty_reset)
```

- Channels **in** `channel_mask` → encoded DShot bit duties (t1h or t0h)
- Channels **NOT in** `channel_mask` → `dshot_duty_reset` (output idles)
- `channel_map_hw_to_motor[timer][hw_ch]` = motor index, or -1 if unused

This matches SAMV7 datasheet (section 52.6.5): "The number of data transfers
depends on the number of SYNCx bits set in PWM_SCM" and dispatch is "to the
synchronized channel with the lowest channel number, then the second lowest, etc."

Since all 4 SYNCx bits are set, every DMAR cycle consumes exactly 4 words.

## Blocker Resolutions

### B1: `io_timer_set_dshot_mode()` — keep existing 2-arg signature

The declaration in `io_timer.h:172` and stub in `io_timer_pwmc.c:848` both have:
```c
int io_timer_set_dshot_mode(uint8_t timer, unsigned dshot_pwm_freq);
```

**No signature change.** The function receives the channel mask via a new shared
variable set by `dshot.c` before calling. Specifically, add to `io_timer_pwmc.c`:
```c
/* Written by dshot.c before calling io_timer_set_dshot_mode() */
static uint32_t g_dshot_channel_mask[MAX_IO_TIMERS];

void io_timer_set_dshot_channel_mask(uint8_t timer, uint32_t mask)
{
    g_dshot_channel_mask[timer] = mask;
}
```
Declare `io_timer_set_dshot_channel_mask()` in `io_timer.h`. This avoids touching
the existing 2-arg signature or any other call sites.

**ORDERING DEPENDENCY:** `io_timer_set_dshot_channel_mask(timer, mask)` MUST be
called before `io_timer_set_dshot_mode(timer, freq)`. The mode function reads
`g_dshot_channel_mask[timer]` to know which channels to configure. This ordering
is enforced in `up_dshot_init()` which is the only caller — documented with a
comment at the call site. No other code path calls these functions.

**LIFECYCLE SAFETY:** `g_dshot_channel_mask[]` is zeroed on two paths:
1. At top of `up_dshot_init()` — `memset(g_dshot_channel_mask, 0, ...)` before
   repopulating, so re-init with different channel_mask never sees stale values.
2. Add `io_timer_dshot_clear_masks()` called from any future `up_dshot_deinit()`
   or from `io_timer_unallocate_channel()` when a DShot channel is freed.
This prevents stale masks leaking between `dshot stop` → `dshot start` cycles.

### B2: `dshot.c` never touches private PWMC register macros

All PWMC register manipulation stays inside `io_timer_pwmc.c`. The `dshot.c` file
only uses:

| What `dshot.c` accesses | Source |
|--------------------------|--------|
| `SAMV7_PWM_DMAR` (0x0024) | NuttX public header `sam_pwm.h` — for DMA dest addr |
| `io_timers[timer].base` | Public `io_timer_hw_description.h` — base address |
| `io_timers[timer].dshot.xdmac_ch_tx` | Public `dshot.h` — DMA PERID |
| `io_timer_set_dshot_mode()` | Public `io_timer.h` — configures PWMC sync mode |
| `io_timer_update_dma_req()` | Public `io_timer.h` — enable/disable WRDY |
| `io_timer_set_enable()` | Public `io_timer.h` — enable/disable channels |
| `io_timer_channel_init()` | Public `io_timer.h` — configure GPIO |
| `sam_dmachannel/dmatxsetup/dmastart/dmastop` | NuttX `sam_xdmac.h` — DMA API |

**`up_dshot_arm()` uses `io_timer_set_enable()`** (like STM32 does), not direct
PWM_ENA/DIS writes. For disarm force-low, add a small helper to `io_timer_pwmc.c`:
```c
void io_timer_dshot_force_low(uint8_t timer);
```
This writes CDTYUPD=0 for each channel in `g_dshot_channel_mask[timer]`, using the
private macros that only `io_timer_pwmc.c` has access to.

### B3: CPOL/CDTY polarity — scope-gated truth table

**The existing code comment is contradictory.** `io_timer_pwmc.c:391` says
"CPOL=1, CDTY=0 means output stays high entire period" but the SAMV7 datasheet
(section 52.6.2.3) for CPOL=1, left-aligned says: output starts HIGH, goes LOW
when counter reaches CDTY.

If CDTY=0, two interpretations exist:
- A: Counter immediately matches 0 → output LOW entire period (idle LOW)
- B: Counter never exceeds 0 on first tick → output HIGH entire period (idle HIGH)

**Resolution:** This MUST be scope-verified before coding the DShot bit encoding.
Step 2 below is explicitly a scope validation step with a minimal test.

The DShot encoding tables will use named constants, not magic numbers:
```c
static uint32_t dshot_duty_bit1[MAX_IO_TIMERS];   /* 75% of CPRD */
static uint32_t dshot_duty_bit0[MAX_IO_TIMERS];   /* 37.5% of CPRD */
static uint32_t dshot_duty_reset[MAX_IO_TIMERS];   /* 0 or CPRD — determined by scope test */
```

If scope shows CDTY=0 → always HIGH (interpretation B), then:
- Reset period: `dshot_duty_reset = CPRD` (inverted) or flip to CPOL=0
- This will be decided based on Step 2 scope results before proceeding

### B4: Per-timer timing arrays, not global scalars

All timing variables are per-timer arrays:
```c
static uint32_t dshot_cprd[MAX_IO_TIMERS];
static uint32_t dshot_duty_bit1[MAX_IO_TIMERS];
static uint32_t dshot_duty_bit0[MAX_IO_TIMERS];
static uint32_t dshot_duty_reset[MAX_IO_TIMERS];
```

This supports future boards with >1 PWM module or mixed DShot rates.

## Files to Create

| File | Purpose |
|------|---------|
| `platforms/nuttx/src/px4/microchip/samv7/dshot/dshot.c` | Main DShot driver (~500 lines) |
| `platforms/nuttx/src/px4/microchip/samv7/dshot/CMakeLists.txt` | Build config |

## Files to Modify

| File | Change |
|------|--------|
| `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c` | Fill `io_timer_set_dshot_mode()`, `io_timer_update_dma_req()`, add `io_timer_set_dshot_channel_mask()`, `io_timer_dshot_force_low()` |
| `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/io_timer.h` | Declare new helpers |
| `platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt` | Add `if(CONFIG_DRIVERS_DSHOT) add_subdirectory(dshot)` |
| `boards/microchip/samv71-xult-clickboards/default.px4board` | Enable `CONFIG_DRIVERS_DSHOT=y` |

## Implementation Steps

### Step 1: Scaffold — `dshot.c` + CMake + board config

Create `dshot/dshot.c` with all 8 API functions as stubs (return 0 or -ENOSYS).
Create `dshot/CMakeLists.txt` (`px4_add_library(arch_dshot dshot.c)`).
Add conditional `add_subdirectory(dshot)` to parent CMake.
Enable `CONFIG_DRIVERS_DSHOT=y` in `default.px4board`.

**Verify:** `make clean && make microchip_samv71-xult-clickboards_default` builds and links.

### Step 2: CPOL polarity scope validation (GATE for all subsequent steps)

Fill `io_timer_set_dshot_mode()` with minimal sync mode config (CH0 only, CPOL=1,
CPRD=500 for DShot150 speed). Write a test that sets CDTY to 0, CPRD/2, and CPRD
and measure on oscilloscope:

| CDTY value | Expected if interp A | Expected if interp B |
|------------|---------------------|---------------------|
| 0 | Always LOW | Always HIGH |
| CPRD/2 | 50% duty (HIGH then LOW) | Same |
| CPRD | Always HIGH | Always LOW |

Record results. Set `dshot_duty_reset[]` accordingly.

**This blocks Steps 5-6** (packet encoding depends on polarity truth table).

Also verify WRDY-as-DMA-gate during this step: enable one sync channel, write
a known CDTY, toggle IER2 WRDY on/off, and confirm XDMAC transfer starts only
when WRDY is enabled. If WRDY does not gate XDMAC requests (i.e. SAMV7 uses a
different mechanism than IER2/IDR2 to connect PWMC→XDMAC), the fallback is
direct XDMAC channel enable/disable via `sam_dmastart()`/`sam_dmastop()` to
gate transfers instead. Document which mechanism works.

### Step 3: Fill `io_timer_set_dshot_mode()` in `io_timer_pwmc.c`

Reads `g_dshot_channel_mask[timer]` (set by `dshot.c` before calling).

1. Disable all 4 channels: `putreg32(0x0F, base + PWM_DIS_OFFSET)`
   (All 4 must be disabled for sync mode reconfiguration)
2. Configure CH0 master: `CMR = (1 << 0) | PWM_CMR_CPOL` (CPRE=1, MCK/2=75MHz)
3. Set CH0 `CPRD = 75MHz / dshot_pwm_freq`
4. Set CDTY = idle value (per Step 2 scope result) for all 4 channels
5. Write SCM: `SCM_SYNC_SEL(0x0F) | SCM_UPDM_MODE2`
   (All 4 channels synchronized — fixed layout, see Buffer Layout Model)
   - PTRM=0 (DMA request at period end)
6. Write `SCUC = SCUC_UPDULOCK`
7. **DO NOT enable channels here.** Leave all channels disabled after sync mode
   configuration. `up_dshot_arm(true)` is the only path that enables outputs,
   preventing accidental output drive during init before the flight controller
   is ready. This matches the arm/disarm safety model.

Store: `dshot_cprd[timer]`, `dshot_duty_bit1[timer]`, `dshot_duty_bit0[timer]`

### Step 4: Fill `io_timer_update_dma_req()` + add new helpers in `io_timer_pwmc.c`

**`io_timer_update_dma_req()`:**
```c
void io_timer_update_dma_req(uint8_t timer, bool enable)
{
    uint32_t base = io_timers[timer].base;
    if (enable) {
        (void)getreg32(base + SAMV7_PWM_ISR2);  /* Clear pending */
        putreg32(IR2_WRDY, base + SAMV7_PWM_IER2);
    } else {
        putreg32(IR2_WRDY, base + SAMV7_PWM_IDR2);
    }
}
```

**`io_timer_set_dshot_channel_mask()`:** Setter for `g_dshot_channel_mask[timer]`.

**`io_timer_dshot_force_low()`:** Writes CDTYUPD=reset_value for each channel in
`g_dshot_channel_mask[timer]`. Called by `dshot.c` on disarm.

**`io_timer_dshot_check_unre()`:** Reads ISR2, returns true if IR2_UNRE was set.

Declare all in `io_timer.h`.

### Step 5: Implement `up_dshot_init()` in `dshot.c`

1. Store `channel_mask` and `dshot_pwm_freq` globally
2. Build per-timer state from `timer_io_channels[]`:
   - `channel_map_hw_to_motor[timer][hw_ch] = motor_index` (-1 if unused)
   - `dshot_timer_channel_mask[timer]` (HW channel bitmask of requested channels)
3. Call `io_timer_channel_init(ch, IOTimerChanMode_Dshot, NULL, NULL)` per channel
4. Call `io_timer_set_dshot_channel_mask(timer, mask)` per timer
   **THEN** call `io_timer_set_dshot_mode(timer, dshot_pwm_freq)` per timer
   (ordering dependency: mask must be set before mode — see B1)
6. Allocate XDMAC per timer:
   ```c
   uint8_t perid = io_timers[timer].dshot.xdmac_ch_tx;
   uint32_t flags = DMACH_FLAG_PERIPHPID(perid)
                  | DMACH_FLAG_PERIPHISPERIPH
                  | DMACH_FLAG_PERIPHWIDTH_32BITS
                  | DMACH_FLAG_PERIPHCHUNKSIZE_1
                  | DMACH_FLAG_MEMWIDTH_32BITS
                  | DMACH_FLAG_MEMINCREMENT
                  | DMACH_FLAG_MEMBURST_1;
   dshot_dma_handle[timer] = sam_dmachannel(0, flags);
   ```
7. Clear DMA buffers + packet arrays
8. Return initialized channel mask

### Step 6: Implement `dshot_motor_data_set()` in `dshot.c`

Build 16-bit DShot packet and pack into DMA buffer:
1. Packet = `(throttle << 5) | (telemetry << 4) | crc`
2. CRC = XOR of three nibbles of bits [15:4]
3. For each of 16 bits (MSB first), for each of 4 HW channels (CH0-CH3):
   - Look up motor from `channel_map_hw_to_motor[timer][hw_ch]`
   - If motor >= 0 **AND** `(channel_mask & (1 << motor))` is set (motor is both
     mapped and requested by the init-time PX4 output mask):
     `buf[bit * 4 + hw_ch] = bit_set ? dshot_duty_bit1[timer] : dshot_duty_bit0[timer]`
   - Otherwise (unmapped OR not in `channel_mask`):
     `buf[bit * 4 + hw_ch] = dshot_duty_reset[timer]`
4. Reset period (index 64-67): all 4 slots = `dshot_duty_reset[timer]`

**Mask semantics:** `channel_mask` (arg to `up_dshot_init()`) uses **PX4 output
channel bits** (bit 0 = Motor 1 = `timer_io_channels[0]`, etc.). The
`channel_map_hw_to_motor[timer][hw_ch]` array translates from HW channel index
(0-3, used for buffer position) to PX4 output channel index (0-3, used for
`dshot_packet[]` lookup). This mapping is built during init by scanning
`timer_io_channels[]`:
```
timer_io_channels[0] → {timer=0, hw_ch=3} → channel_map_hw_to_motor[0][3] = 0  (Motor 1)
timer_io_channels[1] → {timer=0, hw_ch=1} → channel_map_hw_to_motor[0][1] = 1  (Motor 2)
timer_io_channels[2] → {timer=0, hw_ch=2} → channel_map_hw_to_motor[0][2] = 2  (Motor 3)
timer_io_channels[3] → {timer=0, hw_ch=0} → channel_map_hw_to_motor[0][0] = 3  (Motor 4)
```
`dshot_timer_channel_mask[timer]` is in **HW channel bits** (bit 0 = CH0, etc.)
— used for PWMC register operations only. Both masks are built from the same
`timer_io_channels[]` scan but serve different purposes.

### Step 7: Implement `up_dshot_trigger()` in `dshot.c`

**Per-timer `dma_active` flag:** Tracks whether a DMA transfer is in-flight.
Set to `true` after `sam_dmastart()`, cleared in `dshot_dma_callback()`.
This prevents calling `sam_dmastop()` on an already-idle channel, which on
SAMV7 can trigger a spurious non-OK callback that inflates `dshot_dma_errors[]`.

For each timer with active channels:
1. Check UNRE: `if (io_timer_dshot_check_unre(timer)) dshot_underrun_count[timer]++`
2. Stop previous DMA only if active: `if (dma_active[timer]) { sam_dmastop(dshot_dma_handle[timer]); dma_active[timer] = false; }`
3. `__DMB()` memory barrier
4. Clean dcache: `up_clean_dcache((uintptr_t)buf, (uintptr_t)buf + xfer_size)`
5. Setup DMA (fixed 68-word transfer = 17 periods × 4 channels):
   ```c
   uint32_t dmar_addr = io_timers[timer].base + SAMV7_PWM_DMAR;
   uint32_t xfer_size = DSHOT_TOTAL_PERIODS * 4 * sizeof(uint32_t);  /* 68 × 4 = 272 bytes */
   sam_dmatxsetup(dshot_dma_handle[timer], dmar_addr, (uint32_t)buf, xfer_size);
   ```
6. Start DMA: `sam_dmastart(dshot_dma_handle[timer], dshot_dma_callback, (void*)(uintptr_t)timer)`
7. `dma_active[timer] = true`
8. Enable DMA request: `io_timer_update_dma_req(timer, true)`

DMA callback (exact NuttX signature):
```c
static void dshot_dma_callback(DMA_HANDLE handle, void *arg, int result)
{
    uint8_t timer = (uint8_t)(uintptr_t)arg;
    (void)handle;
    dma_active[timer] = false;
    io_timer_update_dma_req(timer, false);
    if (result != OK) { dshot_dma_errors[timer]++; }
}
```

### Step 8: Implement `up_dshot_arm()` in `dshot.c`

```c
int up_dshot_arm(bool armed)
{
    dshot_armed = armed;
    if (armed) {
        return io_timer_set_enable(true, IOTimerChanMode_Dshot,
                                   IO_TIMER_ALL_MODES_CHANNELS);
    } else {
        for (uint8_t t = 0; t < MAX_IO_TIMERS; t++) {
            if (dshot_timer_channel_mask[t]) {
                io_timer_dshot_force_low(t);
            }
        }
        return io_timer_set_enable(false, IOTimerChanMode_Dshot,
                                   IO_TIMER_ALL_MODES_CHANNELS);
    }
}
```

### Step 9: Bidirectional stubs

All return `-ENOSYS` or no-op. Architecture reserved in `dshot_conf_t` for Phase 4+.

## Implementation-Time Hardening Notes

These are non-blocking but must be addressed during coding:

1. **`dma_active[]` concurrency:** Touched from task context (`up_dshot_trigger`)
   and DMA callback IRQ context. Declare as `static volatile bool dma_active[]`.
   Guard the stop-then-start sequence in `up_dshot_trigger()` with
   `irqstate_t flags = enter_critical_section()` / `leave_critical_section(flags)`
   around the `dma_active` check → `sam_dmastop()` → `sam_dmastart()` → set-true
   sequence to prevent a race where the callback fires between check and start.

2. **`io_timer_dshot_clear_masks()` granularity:** When called from
   `io_timer_unallocate_channel()`, clear only the specific HW channel bit for
   that timer — `g_dshot_channel_mask[timer] &= ~(1 << hw_ch)` — not the entire
   timer mask. This preserves partial-channel DShot sessions where some channels
   are freed while others remain active.

3. **WRDY gating is a hard pass/fail gate** in Step 2 verification. If WRDY
   IER2/IDR2 does not control XDMAC request generation, the entire DMA trigger
   path changes. Document the confirmed mechanism in code comments on
   `io_timer_update_dma_req()` once validated.

4. **`sam_dmastart()` failure path:** In `up_dshot_trigger()`, if `sam_dmastart()`
   returns non-OK, `dma_active[timer]` must stay `false` and `io_timer_update_dma_req()`
   must NOT be called (WRDY stays disabled). Only enable WRDY after confirmed DMA start.

5. **`io_timer_dshot_clear_masks()` scope:** Only clear the specific HW channel bit
   on the specific timer being freed. Never touch other timers or other channels
   on the same timer.

6. **`io_timer_unallocate_channel()` hook guard:** The `io_timer_dshot_clear_masks()`
   call in `io_timer_unallocate_channel()` must be gated on `channel_mode == IOTimerChanMode_Dshot`.
   Otherwise unrelated channel frees (PWM, OneShot) would mutate DShot mask state.
   File: `io_timer_pwmc.c`.

7. **Critical-section scope:** The `enter_critical_section()` in `up_dshot_trigger()`
   must wrap only the `dma_active` check → `sam_dmastop()` → `sam_dmastart()` → set-true
   sequence. Do not hold interrupts across dcache clean or buffer setup.
   File: `dshot.c`.

8. **Step 2 is mandatory pass/fail.** Both WRDY gating and CPOL/reset-duty are
   hardware-truth gates. No ESC testing proceeds until both are scope-confirmed.

## Verification

1. **Build:** `make clean && make microchip_samv71-xult-clickboards_default`
2. **Polarity:** Step 2 scope test — truth table for CPOL=1 + CDTY values
3. **Boot:** `dshot start` on NSH — no crash
4. **Scope test:** `actuator_test set -m 1 -v 0.1`
   - Period matches DShot speed (1.67µs for DShot600)
   - Bit-1 high = ~75%, Bit-0 high = ~37.5%
   - 16 bit periods + 1 reset period (output low)
   - Only initialized channels active
5. **UNRE check:** No underrun warnings in log
6. **ESC test:** Connect DShot ESC, `actuator_test set -m 1 -v 0.05` → motor spins
7. **Multi-motor:** All 4 motors independent, no crosstalk
