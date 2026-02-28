# SAMV71 PX4 Port — Known Issues

> **Generated**: 2026-02-28
> **Branch**: `samv7-custom`
> **Source**: Codex static analysis + manual verification

---

## Summary

16 issues identified across two analysis passes. 14 confirmed correct, 1 incorrect (Issue A2), 1 partially correct (Issue B2).

### Issue Index

| ID | Severity | Title | Status |
|----|----------|-------|--------|
| A1 | HIGH | GPIO interrupt backend broken — DRDY callbacks silently never fire | Open |
| A2 | LOW | ~~PWM init drives dangerous level~~ — actually safe (CDTY=0 = 0% duty) | Invalid |
| A3 | HIGH | Safety button PA9 conflicts with enabled UART0 RX | Open |
| A4 | HIGH | Board UUID not unique per-chip — all SAMV71Q21B boards collide | Open |
| A5 | MEDIUM | Reboot-to-bootloader handoff lost across reset (RAM, not GPBR) | Open |
| A6 | MEDIUM | OneShot mode wired in API but io_timer_set_rate() rejects rate < 50 | Open |
| A7 | MEDIUM | Debug-heavy SD/HSMCI flags in flight defconfig (40ms+ SD writes) | Open |
| A8 | LOW | board_critmon is a stub (no real critical-section timing) | Open |
| B1 | CRITICAL | Shared rcS has branch-local disables affecting all boards | Open |
| B2 | MEDIUM | SD mount flow inconsistency — double mount causes EBUSY | Open |
| B3 | CRITICAL | GPS UART2 RX and ICM20689 CS both on PD25 | Open |
| B4 | MEDIUM | RC UART4 RX and SD card-detect both on PD18 | Known/Mitigated |
| B5 | HIGH | Board script starts dataman unconditionally, bypassing rcS gating | Open |
| B6 | MEDIUM | micro_hal.h uses undefined GPIO_PULLUP / GPIO_FLOAT symbols | Open |
| B7 | MEDIUM | PROGMEM flash region not reserved in linker script | Open |
| B8 | LOW | USB VBUS hardcoded "present" — no actual VBUS sense GPIO | Intentional |

---

## Pass 1 — Core Driver / HAL Issues

### A1 — GPIO Interrupt Backend Broken (HIGH)

**Problem**: `sam_gpiosetevent()` has two distinct bugs that cause GPIO interrupts (DRDY, PPS, RPM) to silently never fire.

**Bug A — Handler never attached**: The function receives the `handler` callback parameter but never calls `irq_attach(irq, handler, arg)` to register it with NuttX. The SAMV7 GPIO IRQ dispatch (`sam_gpiointerrupt()` in NuttX `sam_gpioirq.c`) uses `irq_dispatch()` which requires a handler attached via `irq_attach()`. Without it, the interrupt fires but dispatches to nothing.

**Bug B — Wrong argument type**: `sam_gpioirqenable(int irq)` expects a virtual IRQ number (e.g., `SAM_IRQ_PD28`). The code passes `intcfg`, a `gpio_pinset_t` (32-bit config word). Inside `sam_gpioirqenable()`, `sam_irqbase()` fails the range checks and returns `-EINVAL`, so the interrupt enable is a silent no-op.

**Impact**: The ICM20689 driver calls `px4_arch_gpiosetevent()` for DRDY (`ICM20689.cpp:432`). Since it silently returns OK despite doing nothing, the driver thinks DRDY is configured. After the 100ms watchdog timeout, it falls back to polling (`ScheduleOnInterval`). The sensor still works but at reduced efficiency. ICM45686 and BMP388 already use polling, so they are unaffected.

**Fix required**: Rewrite `sam_gpiosetevent()` to:
1. Convert pinset to IRQ number (extract port + pin, compute `SAM_IRQ_Pxn`)
2. Call `irq_attach(irq, handler, arg)`
3. Call `sam_gpioirqenable(irq)` with the IRQ number, not the pinset

**Files**:
- `boards/microchip/samv71-xult-clickboards/src/sam_gpiosetevent.c:101-109`
- `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h:106`
- `platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_gpioirq.c:383` (reference)

---

### A2 — ~~PWM Init Drives Dangerous Level~~ (INVALID)

**Codex claim**: CPOL=1 + CDTY=0 = always-high output = dangerous for ESCs.

**Verdict**: **Incorrect.** SAMV7 PWMC left-aligned mode with CPOL=1 and CDTY=0 gives 0% duty (output is LOW for the entire period). The output starts HIGH at period start and transitions LOW when counter reaches CDTY. If CDTY=0, the transition happens at counter=0, so the output is LOW the entire period. This is safe.

The code comment at `io_timer_pwmc.c:391-393` is misleading:
```c
/* Note: With CPOL=1, CDTY=0 means output stays high entire period */
```
This comment is factually wrong — CDTY=0 gives 0% duty, not 100%. The hardware behavior is safe.

Additionally, `board_on_reset(-1)` runs before any PWMC init, driving all motor pins LOW as GPIO outputs. Even if there were a brief glitch at channel enable, ESCs require sustained 1000us+ pulses to react.

**Action**: Fix the misleading comment. No hardware fix needed.

**File**: `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c:391-393`

---

### A3 — Safety Button PA9 Conflicts with UART0 RX (HIGH)

**Problem**: Both UART0 RX and the safety button claim pin PA9.

- `board_config.h:144` — `GPIO_BTN_SAFETY` = PA9 (GPIO input with pull-up)
- `defconfig:36` — `CONFIG_SAMV7_UART0=y` → NuttX configures PA9 as UART0_RXD

Boot order: NuttX UART init runs first (in `sam_lowputc.c`), then `px4_gpio_init()` reconfigures PA9 as GPIO input for the safety button. Result: UART0 RX is broken, safety button works.

Additionally PB0 (UART0_TXD) is used for Motor 4 (PWMC0_H0). Both UART0 TX and RX pins are stolen, making UART0 completely unusable.

**Impact**: UART0 occupies a `/dev/ttyS` slot but cannot actually send or receive data. Wastes resources and causes confusing device numbering.

**Fix**: Disable UART0 in defconfig: change `CONFIG_SAMV7_UART0=y` to `# CONFIG_SAMV7_UART0 is not set`.

**Files**:
- `boards/microchip/samv71-xult-clickboards/src/board_config.h:144`
- `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig:36`
- `platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/hardware/samv71_pinmap.h:477`

---

### A4 — Board UUID Not Unique Per-Chip (HIGH)

**Problem**: `board_identity.c` reads from `SAMV7_UUID_BASE` (0x400E0940), which is the CHIPID register block (CIDR + EXID). These are family/variant identifiers, not per-chip unique serial numbers.

```c
// Word 0: CIDR — same for ALL SAMV71Q21B chips
// Word 1: EXID — same for ALL SAMV71Q21B chips
// Word 2: chip_uuid[0] ^ (2 << 24) — deterministic, still not unique
```

**Impact**: All SAMV71Q21B boards produce the identical UUID. This causes MAVLink system ID conflicts when multiple boards are on the same network, and `ver uid` returns the same value for every board.

**Fix**: Read from the SAMV7 User Signature area (unique per-chip serial number at 0x00400000-0x0040001F in flash, or use GPNVM User Row). The code already has a comment: "In production, these should be read from Flash User Signature area."

**Files**:
- `platforms/nuttx/src/px4/microchip/samv7/version/board_identity.c:87-96`
- `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h:50`

---

### A5 — Reboot-to-Bootloader Handoff Not Persistent (MEDIUM)

**Problem**: Reset mode is stored in `static uint32_t reset_mode_value` — a plain RAM variable. After `up_systemreset()`, RAM is zeroed by startup code. The value is lost.

```c
// board_reset.cpp:51
static uint32_t reset_mode_value = 0;
// TODO: Use GPBR (General Purpose Backup Registers) at 0x400E1890
```

**Impact**: `reboot -b` (reboot to bootloader) resets the board but the bootloader can't detect it should stay in bootloader mode. Currently academic since no bootloader exists, but must be fixed for Phase 5.

**Fix**: Write `modes[mode] | arg` to SAMV7 GPBR register (0x400E1890-0x400E18FC, survives system reset). Read GPBR in bootloader/early startup.

**Files**:
- `platforms/nuttx/src/px4/microchip/samv7/board_reset/board_reset.cpp:51,85,128`

---

### A6 — OneShot Mode Wired in API but Cannot Be Set (MEDIUM)

**Problem**: The call chain allows OneShot through `pwm_servo.c` but rejects it in `io_timer_pwmc.c`:

1. `pwm_servo.c:122` — `up_pwm_servo_set_rate_group_update()` explicitly allows `PWM_RATE_ONESHOT` (rate=0)
2. Calls `io_timer_set_pwm_rate()` → `io_timer_set_rate()`
3. `io_timer_pwmc.c:471` — `if (rate < 50 || rate > 8000) return -EINVAL`

OneShot (rate=0) passes the servo gate but fails in io_timer. The error is silently swallowed.

**Impact**: Not currently used, but the API pretends to support OneShot when it doesn't.

**Fix**: Add OneShot exemption in `io_timer_set_rate()`, or return -ENOSYS early with a clear message.

**Files**:
- `platforms/nuttx/src/px4/microchip/samv7/io_pins/pwm_servo.c:121-129`
- `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c:471`

---

### A7 — Debug-Heavy SD/HSMCI Flags in Flight Defconfig (MEDIUM)

**Problem**: Three NuttX debug options cause massive SD write overhead:

```
CONFIG_DEBUG_INFO=y           # Global debug info
CONFIG_DEBUG_FS_INFO=y        # Filesystem debug (every VFS call logged)
CONFIG_DEBUG_MEMCARD_INFO=y   # Memory card debug
CONFIG_SAMV7_HSMCI_CMDDEBUG=y # HSMCI command dump (~15 registers per SD command)
```

The HSMCI command debug calls `sam_cmddump()` which prints ~15 register values via `mcinfo()` for every single SD command. Combined with `CONFIG_DEBUG_FS_INFO`, every logger SD write triggers a cascade of printf calls.

**Impact**: SD write latency of 40-80ms average (confirmed by ULog analysis). Rate control loop starved to 46Hz logged rate. Logger + log_writer_file consume ~40% CPU.

**Fix**: Remove from defconfig:
```
# CONFIG_DEBUG_INFO is not set
# CONFIG_DEBUG_FS_INFO is not set
# CONFIG_DEBUG_MEMCARD_INFO is not set
# CONFIG_SAMV7_HSMCI_CMDDEBUG is not set
```

**Files**:
- `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig:192,197`

---

### A8 — board_critmon is a Stub (LOW)

**Problem**: `board_critmon_init()` and `board_critmon_report()` are empty stubs.

**Impact**: No critical section timing monitoring. Not a bug — just unimplemented.

**Fix**: SAMV71 Cortex-M7 has DWT (Data Watchpoint and Trace) with cycle counter, same as STM32H7. Enable `DWT->CYCCNT` and implement the timing functions. Reference: `platforms/nuttx/src/px4/stm/stm32_common/board_critmon/board_critmon.c`.

**File**: `platforms/nuttx/src/px4/microchip/samv7/board_critmon/board_critmon.c:51`

---

## Pass 2 — System Integration Issues

### B1 — Shared rcS Has Branch-Local Disables Affecting All Boards (CRITICAL)

**Problem**: Four features are commented out in the **common** `ROMFS/px4fmu_common/init.d/rcS`:

| Line | Feature | Comment |
|------|---------|---------|
| 74 | SD auto-format | `# TEMPORARILY DISABLED for driver testing` |
| 276 | dataman start | `# TEMPORARILY DISABLED - dataman hangs when no storage available` |
| 542 | navigator start | `# TEMPORARILY DISABLED - navigator requires dataman` |
| 585 | payload_deliverer | `# TEMPORARILY DISABLED - testing dataman_client trigger` |

This file is shared by ALL PX4 boards. Merging this branch to main would break dataman, navigator, and payload delivery for every Pixhawk, CUAV, Holybro, etc.

**Impact**: Merge blocker. Any PR from this branch will carry these global disables.

**Fix**: Revert all four changes in `rcS` to upstream state. Use board-specific scripts (`rc.board_defaults`, `rc.board_extras`) for SAMV71-specific workarounds.

**File**: `ROMFS/px4fmu_common/init.d/rcS:74,276,542,585`

---

### B2 — SD Mount Flow Inconsistency — Double Mount (MEDIUM)

**Problem**: The SD card is mounted twice during boot:

1. **Board init** (`init.c:157`): `mount("/dev/mmcsd0", "/fs/microsd", "vfat", 0, NULL)` — mounts early, returns OK even if mount fails (line 165: `return OK`)
2. **rcS** (line 58): `if mount -t vfat /dev/mmcsd0 /fs/microsd` — tries to mount again

If the board code already mounted successfully, the rcS `mount` returns EBUSY (already mounted). The rcS logic treats non-zero return as failure and keeps `STORAGE_AVAILABLE=no`.

**Impact**: `STORAGE_AVAILABLE=no` may cause some storage-dependent startup features to be skipped (startup tune, format flow). In practice, logging and params still work because the SD is actually mounted from the early mount.

**Fix**: Either:
- Remove the early mount from `init.c` and let rcS handle it entirely, OR
- Add `umount /fs/microsd` in rcS before the mount attempt, OR
- Check if already mounted in rcS: `if mountpoint -q /fs/microsd; then set STORAGE_AVAILABLE yes`

**Files**:
- `boards/microchip/samv71-xult-clickboards/src/init.c:157,165`
- `ROMFS/px4fmu_common/init.d/rcS:58,72`

---

### B3 — GPS UART2 RX and ICM20689 CS Both on PD25 (CRITICAL)

**Problem**: Pin PD25 is claimed by two peripherals simultaneously:

- `GPIO_UART2_RXD` = PD25 (Peripheral C) — `samv71_pinmap.h:485`
- `GPIO_SPI0_CS_ICM20689` = PD25 (GPIO output) — `board_config.h:78`

NuttX UART2 init (`sam_lowputc.c:289`) configures PD25 as UART2 peripheral. Later, SPI driver configures PD25 as GPIO output (CS for ICM20689). The last configuration wins.

**Impact**: GPS cannot receive data when ICM20689 CS is configured. This is a **hard hardware conflict** on the SAMV71-XULT dev board — PD25 cannot serve both functions simultaneously.

**Fix options**:
1. Move ICM20689 CS to an unused GPIO pin (requires board rework / jumper wire)
2. Disable UART2 (`# CONFIG_SAMV7_UART2 is not set`) when using ICM20689 (no GPS)
3. Use ICM45686 instead (different CS pin, uses polling not DRDY) and disable ICM20689
4. Resolve on custom PCB by assigning separate pins (Phase 5)

**Files**:
- `boards/microchip/samv71-xult-clickboards/src/board_config.h:78`
- `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig:37` (CONFIG_SAMV7_UART2=y)
- `platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/hardware/samv71_pinmap.h:485`

---

### B4 — RC UART4 RX and SD Card-Detect Both on PD18 (MEDIUM)

**Problem**: Pin PD18 is claimed by two functions:

- `GPIO_UART4_RXD` = PD18 (Peripheral C) — `samv71_pinmap.h:492`
- `GPIO_HSMCI0_CD` = PD18 (GPIO input + IRQ) — `board_config.h:245`

`sam_hsmci_initialize()` configures PD18 as GPIO with IRQ and attaches a card-detect interrupt handler (`sam_hsmci.c:264-266`). Then NuttX UART4 init reconfigures PD18 as UART4 RXD peripheral, stealing the pin.

**Mitigation**: This is a documented known conflict. Rathi's fork hardcodes `sam_cardinserted()` to return `true` and disables CD pin usage. The UART4 peripheral mode disables PIO interrupt sensitivity, so spurious card-detect interrupts are unlikely.

**Residual risk**: The `irq_attach()` for the card-detect handler remains in the IRQ table even after UART4 takes the pin. If UART4 peripheral mode is ever disabled, the stale handler could fire.

**Fix**: Skip CD GPIO/IRQ setup entirely when UART4 is enabled. Pass `0` for both `cdcfg` and `cdirq` to `sam_hsmci_initialize()`, as Rathi's fork does with `sam_hsmci_initialize(0, 0)`.

**Files**:
- `boards/microchip/samv71-xult-clickboards/src/board_config.h:245-247`
- `boards/microchip/samv71-xult-clickboards/src/sam_hsmci.c:264-266`
- `boards/microchip/samv71-xult-clickboards/src/init.c:132`

---

### B5 — Board Script Starts Dataman Unconditionally (HIGH)

**Problem**: `rc.board_defaults:51` starts dataman without gating:

```sh
dataman start -f /fs/mtd_waypoints
```

This bypasses the normal rcS gating (which checks `SYS_DM_BACKEND` param and storage availability). Since rcS dataman is disabled globally (Issue B1), this board-level workaround is the only dataman start path.

**Impact**: If QSPI init fails or the `mtd_waypoints` partition doesn't exist, dataman start may hang or crash. The unconditional start also means no fallback to RAM-based dataman.

**Fix**: Re-enable dataman in the common rcS (fix Issue B1 first), then remove the board-level `dataman start` from `rc.board_defaults`. If board-level start is kept, add a guard:
```sh
if [ -b "/dev/mtdblock2" ]; then
    dataman start -f /fs/mtd_waypoints
fi
```

**Files**:
- `boards/microchip/samv71-xult-clickboards/init/rc.board_defaults:51`
- `ROMFS/px4fmu_common/init.d/rcS:276`

---

### B6 — micro_hal.h Uses Undefined GPIO Symbols (MEDIUM)

**Problem**: Two macros in the SAMV7 micro_hal.h use GPIO symbols that don't exist in SAMV7 NuttX headers:

```c
// micro_hal.h:113
#define PX4_MAKE_GPIO_EXTI(gpio) (... | (GPIO_INT|GPIO_INPUT|GPIO_PULLUP))
//                                                             ^^^^^^^^^^
//                                              SAMV7 defines GPIO_CFG_PULLUP, not GPIO_PULLUP

// micro_hal.h:117
#define PX4_GPIO_PIN_OFF(def) (... | (GPIO_INPUT|GPIO_FLOAT))
//                                               ^^^^^^^^^^
//                                   SAMV7 has no GPIO_FLOAT (use GPIO_CFG_DEFAULT)
```

The SAMV7 NuttX GPIO header (`sam_gpio.h:71`) defines `GPIO_CFG_PULLUP`, not `GPIO_PULLUP`. And `GPIO_FLOAT` is not defined at all.

**Impact**: These macros compile without error because they are never instantiated in the current build. Enabling `CONFIG_DRIVERS_PPS_CAPTURE=y` or `CONFIG_DRIVERS_RPM_CAPTURE=y` (which use `PX4_MAKE_GPIO_EXTI` via `PPSCapture.cpp:99` and `RPMCapture.cpp:87`) would cause a build failure.

**Fix**:
```c
#define PX4_MAKE_GPIO_EXTI(gpio) (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INT|GPIO_INPUT|GPIO_CFG_PULLUP))
#define PX4_GPIO_PIN_OFF(def) (((def) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INPUT|GPIO_CFG_DEFAULT))
```

**File**: `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h:113,117`

---

### B7 — PROGMEM Flash Region Not Reserved in Linker Script (MEDIUM)

**Problem**: The defconfig enables PROGMEM (programmable flash storage) at the top of flash:

```
CONFIG_SAMV7_PROGMEM=y
CONFIG_SAMV7_PROGMEM_NSECTORS=2    # 2 x 128KB = 256KB reserved at top of flash
```

But the linker script gives the application the full 2048KB:

```
flash (rx)  : ORIGIN = 0x00400000, LENGTH = 2048K
```

The NuttX PROGMEM driver (`sam_progmem.c`) reserves the last 2 sectors at runtime. If the firmware image grows into this region, the linker will place code there, creating a silent collision where PROGMEM writes corrupt code or vice versa.

**Impact**: Current build uses 64.53% flash (~1320KB), leaving ~470KB before collision. Safe today but a time bomb as features are added.

**Fix**: Change linker script:
```
flash (rx)  : ORIGIN = 0x00400000, LENGTH = 1792K    /* 2048K - 256K PROGMEM */
```

**Files**:
- `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig:169-170`
- `boards/microchip/samv71-xult-clickboards/nuttx-config/scripts/script.ld:42`
- `platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_progmem.c:123` (reference)

---

### B8 — USB VBUS Hardcoded "Present" (LOW)

**Problem**: `board_read_VBUS_state()` always returns `PX4_OK` (0), meaning VBUS is always reported as present:

```c
// usb.c:109
__EXPORT int board_read_VBUS_state(void)
{
    return 0;  /* 0 = PX4_OK = VBUS present */
}
```

**Impact**: `cdcacm_autostart` always thinks USB is connected and will start CDC/ACM. Since `SYS_USB_AUTO=0` is set in board defaults (preventing duplicate MAVLink instances), this doesn't cause a functional issue.

**Mitigation**: This is intentional — the SAMV71-XULT dev board doesn't have a VBUS sense GPIO wired to the MCU. The code includes a TODO comment acknowledging it's a placeholder.

**Fix (custom PCB)**: Wire VBUS to an ADC or GPIO pin and implement actual detection. For the dev board, the current behavior is acceptable.

**File**: `boards/microchip/samv71-xult-clickboards/src/usb.c:109`

---

## Pin Conflict Summary

| Pin | Function A | Function B | Function C | Resolution |
|-----|-----------|-----------|-----------|------------|
| PA9 | Safety Button (GPIO) | UART0 RXD | — | Safety wins; disable UART0 in defconfig |
| PB0 | Motor 4 (PWMC0_H0) | UART0 TXD | — | Motor wins; disable UART0 in defconfig |
| PD18 | RC Input (UART4 RXD) | SD Card Detect (GPIO IRQ) | TC5 (future) | RC wins; disable CD, pass 0 to hsmci_init |
| PD25 | IMU CS (GPIO output) | GPS (UART2 RXD) | — | **Hard conflict** — cannot coexist on dev board |
| PD28 | IMU DRDY (GPIO IRQ) | MCAN0 RX (future) | — | IMU now; remap MCAN0 for custom PCB |

---

## Priority Fix Order

### Before Next Flight
1. **A7** — Remove debug flags from defconfig (5 min)
2. **A3** — Disable UART0 in defconfig (1 min)

### Before Merge to Main
3. **B1** — Revert all rcS global disables (10 min)
4. **B5** — Remove unconditional dataman start from board script (after B1 is fixed)

### Before Production
5. **A1** — Fix GPIO interrupt backend (sam_gpiosetevent rewrite)
6. **A4** — Implement unique UUID from User Signature area
7. **B3** — Resolve PD25 pin conflict (board rework or custom PCB)
8. **B7** — Reserve PROGMEM region in linker script
9. **B6** — Fix undefined GPIO symbols in micro_hal.h
10. **A5** — Implement GPBR-based reset mode persistence
11. **A6** — Add OneShot support or return -ENOSYS cleanly
12. **B2** — Fix SD double-mount flow
13. **B4** — Skip CD GPIO/IRQ setup when UART4 is enabled
14. **A8** — Implement board_critmon with DWT cycle counter
15. **B8** — Add VBUS sense GPIO on custom PCB
