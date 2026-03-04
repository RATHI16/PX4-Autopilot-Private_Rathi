# SAMV71-XULT Post-Phase 3 Changelog

**Date:** 2026-03-04
**Branch:** `samv7-custom`
**Baseline:** Commit `2bb3283810` (feat(samv71): Phases 0-3)
**Build verified:** 958/958 targets, Flash 1,405,924 B (76.62%), SRAM 52,020 B (15.88%)
**Board test:** All 21 unit tests passed, no reboot loop, heartbeat stable

---

## Summary

This commit fixes **5 critical bugs** introduced in the Phases 0-3 commit (`2bb3283810`) that prevented the board from booting reliably. It also adds **3 missing features** that were either overlooked or not included in that commit. All changes have been verified on hardware with a clean build, flash, and full test suite run.

---

## Bug Fixes

### 1. HAS_PROGMEM Boot Hang (Critical)

**File:** `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h`

**Problem:** The Phase 3 commit enabled `HAS_PROGMEM` to support hardfault crash dump storage via internal flash (PROGMEM). On boot, `board_hardfault_init()` calls `progmem_dump_initialize()`, which calls `up_progmem_write()` in NuttX's `sam_progmem.c`. This function executes from **flash** (not RAM) while issuing EEFC flash write commands. On SAMV7, the flash bus stalls during EEFC operations, causing the CPU to hang because it cannot fetch the next instruction from the same flash being written.

**Root Cause:** NuttX's `sam_progmem.c` has `up_progmem_write()` and `up_progmem_eraseblock()` **not** marked as `__ramfunc__`. The lower-level `sam_eefc_command()` in `sam_eefc.c` IS `__ramfunc__`, but the callers are not. When the caller runs from flash and calls EEFC write, the flash bus stalls and the CPU hangs.

**Fix:** Disabled `HAS_PROGMEM` with `#if 0 &&` guard and added a detailed TODO comment explaining the issue. The underlying fix requires patching NuttX's `sam_progmem.c` to add `__ramfunc__` to the write/erase functions.

**Effect:** Board boots reliably. Hardfault crash dump storage is temporarily unavailable until the NuttX fix is applied. The `px4_savepanic()` macro becomes a no-op `(0)`.

```c
/* DISABLED: progmem_dump_initialize() hangs on SAMV7 because NuttX's
 * up_progmem_write() (sam_progmem.c) is not __ramfunc__. Flash bus stalls
 * during EEFC write operations cause the board to hang at boot.
 * TODO: Fix NuttX sam_progmem.c to use __ramfunc__ for write/erase, then
 * re-enable HAS_PROGMEM here.
 */
#if 0 && defined(CONFIG_BOARD_CRASHDUMP) && defined(CONFIG_SAMV7_PROGMEM)
```

---

### 2. board_hardfault_init() Guard (Defensive)

**File:** `boards/microchip/samv71-xult-clickboards/src/init.c`

**Problem:** With `HAS_PROGMEM` disabled, `board_hardfault_init()` is technically a no-op (it has `#ifdef HAS_PROGMEM` checks internally), but the call was unconditional in `board_app_initialize()`. This is confusing and risks calling into crash dump code if the guards are ever accidentally removed.

**Fix:** Wrapped the `board_hardfault_init()` call in `#ifdef HAS_PROGMEM` so it is only called when PROGMEM crash dump support is actually enabled.

**Effect:** Cleaner boot path, no unnecessary function call, self-documenting code.

```c
#ifdef HAS_PROGMEM
	if (board_hardfault_init(2, true) != 0) {
		led_on(LED_RED);
		syslog(LOG_ERR, "[boot] Hardfault init FAILED\n");
	}
#endif
```

---

### 3. hardfault_log Command Build Error

**File:** `boards/microchip/samv71-xult-clickboards/default.px4board`

**Problem:** The `hardfault_log` system command was enabled (`CONFIG_SYSTEMCMDS_HARDFAULT_LOG=y`) but it requires `HAS_PROGMEM` to be defined for the `dump_s` type and progmem APIs. With `HAS_PROGMEM` disabled (fix #1), the build fails with `unknown type name 'dump_s'`.

**Fix:** Disabled `CONFIG_SYSTEMCMDS_HARDFAULT_LOG` with a comment explaining the dependency.

**Effect:** Clean build. The `hardfault_log` NSH command is unavailable (expected — no crash dump storage backend).

```
# CONFIG_SYSTEMCMDS_HARDFAULT_LOG is not set  # Requires HAS_PROGMEM (disabled — sam_progmem.c hang)
```

---

### 4. HARDFAULT_ULOG_PATH Missing Definition

**File:** `boards/microchip/samv71-xult-clickboards/src/board_config.h`

**Problem:** When `HAS_PROGMEM` is disabled, `hardfault_log.h` does not define `HARDFAULT_ULOG_PATH`. However, `log_writer_file.cpp` references this macro (guarded by `defined(px4_savepanic)`, which is always true since we define it as a no-op macro). This causes a build error: `'HARDFAULT_ULOG_PATH' was not declared`.

**Fix:** Added a `#ifndef HARDFAULT_ULOG_PATH` fallback in `board_config.h` that provides an SD card path.

**Effect:** Clean build regardless of `HAS_PROGMEM` state. Logger can still reference the path without errors.

```c
#ifndef HARDFAULT_ULOG_PATH
#define HARDFAULT_ULOG_PATH "/fs/microsd"
#define HARDFAULT_MAX_ULOG_FILE_LEN 80
#endif
```

---

### 5. WDT Reboot Loop with Embedded Metadata (Critical)

**File:** `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`

**Problem:** The Phase 3 commit enabled the hardware watchdog (`CONFIG_SAMV7_WDT=y`). When `CONFIG_BOARD_EXTERNAL_METADATA` was later disabled (to embed metadata in ROMFS for QGC compatibility), the CROMFS image grew by ~70KB. This increased boot time beyond the WDT default timeout (~16 seconds), causing an infinite reboot loop.

**Root Cause:** SAMV7's WDT is enabled by default after power-on with a ~16s timeout (32768Hz slow clock / 128 prescaler). When NuttX has `CONFIG_SAMV7_WDT=y`, it skips disabling the WDT on startup (expecting the application to feed it). PX4 has no `watchdog_init()`/`watchdog_pet()` HAL implementation for SAMV7 (only STM32 has one), so the WDT is never fed and fires during the longer boot sequence.

**Fix:** Disabled `CONFIG_SAMV7_WDT` and `CONFIG_WATCHDOG` in defconfig. When these are not set, NuttX's SAMV7 startup code explicitly disables the WDT via the WDT_MR register early in boot.

**Effect:** No reboot loop regardless of ROMFS size. WDT protection is temporarily lost until a SAMV7-specific `watchdog_init()`/`watchdog_pet()` HAL is implemented (similar to STM32's `iwdg.c`).

**Future Work:** Implement SAMV7 watchdog HAL that:
1. Configures WDT with appropriate timeout (e.g., 26s like STM32)
2. Feeds WDT early in `board_app_initialize()`
3. Integrates with PX4's `watchdog_pet()` periodic feeding

```
# CONFIG_SAMV7_WDT is not set
# CONFIG_WATCHDOG is not set
```

---

## New Features

### 6. QGC Actuators Tab Support (Embedded Metadata)

**File:** `boards/microchip/samv71-xult-clickboards/default.px4board`

**Problem:** `CONFIG_BOARD_EXTERNAL_METADATA=y` was set, which tells the PX4 build system to NOT embed `actuators.json.xz`, `parameters.json.xz`, and `events/all_events.json.xz` into the ROMFS. This is intended for boards with companion computers that serve metadata externally. Without embedded metadata, QGroundControl's Actuators tab shows no motor/servo configuration UI.

**Fix:** Changed to `# CONFIG_BOARD_EXTERNAL_METADATA is not set` to embed all metadata files in ROMFS.

**Effect:** QGroundControl can now display the Actuators tab with motor/servo assignment UI. Flash usage increased from ~63% to ~77% due to the embedded metadata (~200KB compressed).

**Note:** This is a clean build requirement — changing this setting requires `make clean` because the ROMFS/CROMFS image is cached and not regenerated on incremental builds.

---

### 7. MAVLink Development Dialect

**File:** `boards/microchip/samv71-xult-clickboards/default.px4board`

**Problem:** No MAVLink dialect was specified, defaulting to "standard". The development dialect includes additional messages useful for debugging and advanced features (e.g., `DEBUG_VECT`, `DEBUG_FLOAT_ARRAY`, `NAMED_VALUE_FLOAT`, `NAMED_VALUE_INT`).

**Fix:** Added `CONFIG_MAVLINK_DIALECT="development"`.

**Effect:** Full development MAVLink message set available. Enables debugging tools in QGC and other GCS software.

---

### 8. Safety Button and Battery ADC Startup

**Files:**
- `boards/microchip/samv71-xult-clickboards/init/rc.board_defaults`
- `boards/microchip/samv71-xult-clickboards/init/rc.board_sensors`

**Problem:** The `safety_button start` and `board_adc start` commands were missing from the board startup scripts. Although the drivers were enabled in `default.px4board` (`CONFIG_DRIVERS_SAFETY_BUTTON=y`, `CONFIG_DRIVERS_ADC_BOARD_ADC=y`), they were never started during boot.

**Fix:**
- Added `safety_button start` in `rc.board_defaults` (SW0 on PA9)
- Added `board_adc start` in `rc.board_sensors` (AFEC0 for battery voltage/current)

**Effect:**
- Safety button (SW0) now works — pressing toggles armed/disarmed state, LED1 (PC9) indicates safety status
- Battery voltage (PD30/AFEC0_AD0) and current (PA18/AFEC0_AD7) monitoring now active
- QGC shows battery status when voltage divider hardware is connected

---

## Files Changed Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `micro_hal.h` | Bug fix | Disable HAS_PROGMEM (boot hang) |
| `init.c` | Bug fix | Guard board_hardfault_init behind HAS_PROGMEM |
| `default.px4board` | Bug fix + Feature | Disable hardfault_log, embed metadata, add MAVLink dialect |
| `board_config.h` | Bug fix | Add HARDFAULT_ULOG_PATH fallback |
| `defconfig` | Bug fix | Disable WDT (reboot loop) |
| `rc.board_defaults` | Feature | Add safety_button start |
| `rc.board_sensors` | Feature | Add board_adc start |

---

## Verified Boot Log

```
PX4 git-hash: 2bb328381064eac76743e319f77cfb8184a722c0
PX4 version: 1.17.0 40 (17891392)
OS: NuttX Release 11.0.0
MCU: SAMV70, rev. B
PX4GUID: 001700000000003138533448533230313033  (real unique ID)

Board defaults: /etc/init.d/rc.board_defaults
dataman: /fs/mtd_waypoints (68528 bytes)
PWM servo init: mask=0xf (4 channels)
Board mavlink: CDC/ACM enabled
Logger: started (mode=all)
```

All 21 unit tests passed (`tests all` — LED, time, UART, bitset, BSON, file, float, int, I2C/SPI CLI, queues, lists, math, matrix, param, parameters, perf, search_min, sleep, versioning).

---

## Known Issues (Unchanged)

1. **HAS_PROGMEM disabled** — No crash dump storage. Requires NuttX `sam_progmem.c` fix (`__ramfunc__` on write/erase functions)
2. **WDT disabled** — No hardware watchdog protection. Requires SAMV7 watchdog HAL implementation
3. **bmi088_i2c command not found** — Driver enabled in px4board but binary not built. Needs investigation of CMake/build system
4. **MAVLink ttyACM0 contention** — When NSH console is connected on ttyACM0, MAVLink fails to open the same port. Use separate USB ports (J501=NSH, J500=MAVLink) or configure MAVLink on a different serial port
5. **Sensor "no device on bus" warnings** — Expected when Click boards are not physically connected

---

## Build & Flash Commands

```bash
# Clean build (required after metadata/defconfig changes)
make clean && make microchip_samv71-xult-clickboards_default

# Flash via OpenOCD
openocd -f interface/cmsis-dap.cfg -f target/atsamv.cfg \
  -c "init; reset halt; flash write_image erase \
  build/microchip_samv71-xult-clickboards_default/microchip_samv71-xult-clickboards_default.bin \
  0x00400000; reset run; shutdown"

# Run unit tests (from NSH console)
tests all
```
