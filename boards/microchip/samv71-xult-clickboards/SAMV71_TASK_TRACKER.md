# SAMV71 PX4 Production Port — Task Tracker

**Created:** 2026-04-02
**Reference codebase:** PX4-Autopilot-Rathi `samv7-custom` @ `be2a93b270` (flight-tested)
**Team:** Bhanu (software), Rathi (hardware/flight test)

---

## How to Use This Tracker

- **P0 = CRITICAL** — Must be done before next flight test. Fixes known flight-test failures.
- **P1 = HIGH** — Required for production-quality port. Can be parallelized.
- **P2 = MEDIUM** — Improves capability. Do after P0+P1 or when convenient.
- **Effort:** S = hours, M = 1-2 days, L = 3-5 days
- **Blocked by** = cannot start until dependency is done

---

## P0 — CRITICAL (Pre-Flight-Test Fixes)

These 6 items fix issues directly observed in the 2026-03-25 flight tests. They can mostly be done in a single focused session.

| # | Task | Effort | Blocked By | Status |
|---|------|--------|------------|--------|
| P0.1 | Start `board_adc` + battery voltage/current scaling params | S | — | [ ] |
| P0.2 | Start `safety_button` driver | S | — | [ ] |
| P0.3 | Clean up production parameter defaults | S | — | [ ] |
| P0.4 | Fix `board_critmon` stub → real DWT cycle counter impl | S-M | — | [ ] |
| P0.5 | Enable watchdog (WDT) in defconfig + init | S-M | P0.4 | [ ] |
| P0.6 | Enable UART DMA for GPS (UART2) and RC (UART4) | S | — | [ ] |

### P0.1 — Start board_adc and configure battery telemetry
**Owner:**
**Problem:** `board_adc` never started. MAVLink battery reports 0xFFFF. Flight tests had zero battery data.
**Files:**
- `init/rc.board_sensors` — add `board_adc start`
- `init/rc.board_defaults` — add `BAT1_V_DIV`, `BAT1_A_PER_V`, `BAT1_N_CELLS=4`, `BAT1_SOURCE=0`
- `src/board_config.h` — verify `BOARD_ADC_BRICK_VALID`
**Ref:** `boards/nxp/fmuk66-v3/init/rc.board_defaults`, `boards/px4/fmu-v6x/init/rc.board_sensors`
**Verify:** `listener battery_status` shows valid voltage/current

### P0.2 — Start safety_button driver
**Owner:**
**Problem:** Hardware exists (PA9 button, PC9 LED), driver never started.
**Files:**
- `init/rc.board_defaults` — add `safety_button start`
**Ref:** `boards/px4/fmu-v6x/init/rc.board_defaults`
**Verify:** Press safety button → LED toggles, arming gated until safety off

### P0.3 — Clean up production parameter defaults
**Owner:**
**Problem:** Test-era settings weaken safety: `SYS_HAS_GPS=0` (GPS works!), `COM_CPU_MAX=-1`, `COM_ARM_MAG_ANG=-1`, `COM_ARM_HFLT_CHK=0`
**Files:**
- `init/rc.board_defaults` — full review pass
**Changes:**
- `SYS_HAS_GPS=1`
- `COM_CPU_MAX=90` (or default)
- `COM_ARM_MAG_ANG=45` (or default)
- Review all `CBRK_*` — document which are intentional
- Keep `COM_ARM_HFLT_CHK=0` until P1.2 done (document why)
**Ref:** `boards/px4/fmu-v6x/init/rc.board_defaults`

### P0.4 — Fix board_critmon stub (CPU/RAM load reporting)
**Owner:**
**Problem:** Stub implementation. `load_mon` can't report CPU/RAM. Flight tests: "Preflight Fail: No CPU and RAM load information"
**Files:**
- `platforms/nuttx/src/px4/microchip/samv7/board_critmon/board_critmon.c`
**Implementation:**
- Use Cortex-M7 DWT CYCCNT register (same as STM32/i.MX RT)
- Enable: `DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk`
- Read: `DWT->CYCCNT`, convert to timespec @ 300 MHz
**Ref:** `platforms/nuttx/src/px4/stm/stm32_common/board_critmon/board_critmon.c`
**Verify:** `top` shows CPU% per task; preflight warning gone

### P0.5 — Enable watchdog (WDT)
**Owner:**
**Blocked by:** P0.4 (need load_mon working to feed watchdog)
**Problem:** No watchdog at all in Rathi's code. System hangs are unrecoverable.
**Files:**
- `nuttx-config/nsh/defconfig` — `CONFIG_WATCHDOG=y`, `CONFIG_SAMV7_WDT=y`
- `src/init.c` — WDT init
**CAUTION:** Mother repo had WDT reboot loop (commit `d4736c166d`). Set timeout long enough for full boot (10-15s). Port the mother repo fix.
**Verify:** Kill a high-pri task → system resets within timeout

### P0.6 — Enable UART DMA for GPS and telemetry
**Owner:**
**Problem:** GPS/RC in polling mode wastes CPU. Flight tests showed 42-43% CPU.
**Files:**
- `nuttx-config/nsh/defconfig` — `CONFIG_SAMV7_UART2_DMA=y`, `CONFIG_SAMV7_UART4_DMA=y`
**Verify:** `top` shows reduced CPU%; `gps status` no data errors

---

## P1 — HIGH (Production Quality)

These items are required for a production-grade PX4 port. They can be worked in parallel by multiple people.

| # | Task | Effort | Blocked By | Status |
|---|------|--------|------------|--------|
| P1.1 | Tone alarm driver (TC waveform or GPIO buzzer) | M | — | [ ] |
| P1.2 | Fix NuttX `sam_progmem.c` `__ramfunc__` + enable hardfault logging | M-L | — | [ ] |
| P1.3 | DShot ESC protocol (PWMC Sync + XDMAC) | L | — | [ ] |
| P1.4 | Bootloader for SAMV7 | L | P1.2 | [ ] |
| P1.5 | Persistent reset state via GPBR registers | S-M | — | [ ] |
| P1.6 | USB VBUS detection (replace stub) | S-M | — | [ ] |
| P1.7 | CAN / UAVCAN integration (MCAN) | M-L | — | [ ] |
| P1.8 | RC input capture via TC5 | M | — | [ ] |
| P1.9 | Ethernet / MAVLink UDP (KSZ8061 PHY) | M-L | — | [ ] |
| P1.10 | Strengthen board identity (UUID) | S-M | — | [ ] |

### Suggested Parallel Tracks

**Track A (Bhanu — platform drivers):** P1.1, P1.2, P1.3, P1.5
**Track B (Rathi — board integration + HW test):** P1.6, P1.7, P1.8, P1.9
**Either:** P1.4 (after P1.2), P1.10

---

## P2 — MEDIUM (Capability Improvements)

| # | Task | Effort | Blocked By | Status |
|---|------|--------|------------|--------|
| P2.1 | Enable Gyro FFT module (vibration analysis) | S | — | [ ] |
| P2.2 | Enable temperature compensation module | S | — | [ ] |
| P2.3 | Expose additional I2C buses (TWIHS1/TWIHS2) | M | — | [ ] |
| P2.4 | Expose additional UART/USART ports | M | — | [ ] |
| P2.5 | LED PWM driver (brightness/breathing) | M | — | [ ] |
| P2.6 | uXRCE-DDS / ROS2 bridge | S | P1.9 | [ ] |
| P2.7 | Board HW version/revision detection (for custom PCB) | M | — | [ ] |

---

## Not In Scope (Demo Board)

These are tracked for the custom PCB phase but NOT planned for the XULT demo board:

| Feature | Why Deferred |
|---------|-------------|
| 8-channel PWM | Needs PWMC1 or additional TC — custom PCB Phase 5 |
| PX4IO coprocessor | Requires separate STM32 on custom PCB |
| Triple IMU redundancy | Single SPI bus on XULT — custom PCB Phase 6 |
| Expanded ADC (RSSI, 5V sense) | Custom PCB Phase 6 |
| Fixed-wing / VTOL modules | MC-only for now |
| Camera trigger/capture | Application-specific |
| Gimbal control | Application-specific |
| RAMTRON FRAM | Alternative to QSPI (not needed) |
| SPI RGB LED DMA | Niche |

---

## Dependency Graph

```
P0.4 (board_critmon) ──→ P0.5 (watchdog)

P1.2 (progmem fix) ──→ P1.4 (bootloader)

P1.9 (ethernet) ──→ P2.6 (ROS2 bridge)
```

All other tasks are independent and can be parallelized.

---

## Quick Wins (< 1 hour each)

If you have a spare hour, grab one of these:
1. **P0.1** — Add `board_adc start` + 4 param lines
2. **P0.2** — Add `safety_button start` (1 line)
3. **P0.6** — Add 2 lines to defconfig for UART DMA
4. **P2.1** — Add 1 line to px4board for Gyro FFT
5. **P2.2** — Add 1 line to px4board for temp compensation

---

## Flight Test Observations → Task Mapping

| Flight Test Issue | Root Cause | Fix Task |
|-------------------|-----------|----------|
| Battery telemetry = 0xFFFF | `board_adc` not started | P0.1 |
| "No CPU and RAM load information" | `board_critmon` is a stub | P0.4 |
| "Heading estimate not stable" | Mag calibration + defaults | P0.3 (partial) |
| No audible alerts | No tone alarm driver | P1.1 |
| Z-vibration 106 m/s² | Mechanical + no FFT analysis | P2.1 |
| Baro drift 30-60m | Mechanical shielding + temp comp | P2.2 |
| GCS link dropout 2.6s | USB/serial buffering | P0.6 + P1.6 |
| Crashes undiagnosable | Hardfault logging disabled | P1.2 |
| No ESC telemetry | No DShot | P1.3 |

---

## Totals

| Priority | Count | Effort Range |
|----------|-------|-------------|
| P0 CRITICAL | 6 | 4-8 person-days |
| P1 HIGH | 10 | 12-22 person-days |
| P2 MEDIUM | 7 | 5-10 person-days |
| **Total** | **23** | **21-40 person-days** |

---

## Notes

- **Reference codebase is Rathi's repo** (`PX4-Autopilot-Rathi`), not the mother repo (`PX4-Autopilot-Private`). Mother repo has more recent commits but may have build issues.
- **Mother repo fixes to forward-port:** WDT reboot loop fix (`d4736c166d`), Phases 0-3 fixes (`2bb3283810`), boot fixes (`d4736c166d`)
- **NuttX changes** (P1.2 progmem fix) require `make clean` after modifying submodule sources
- **Never** `cat /dev/ttyACM0` from a host terminal — locks USB CDC and hangs the board
- **Pin conflicts:** PA9 (Safety vs UART0), PB0 (Motor 4 vs UART0), PD18 (SD CD vs TC5/RC)
