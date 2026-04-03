# SAMV71 PX4 Port — Comprehensive Implementation Plan

## Context

The SAMV71-XULT PX4 port is at ~45% feature parity with FMUv6X. 16+ known issues have been identified (Codex analysis + manual verification + deeper review), a full gap analysis exists, and Rathi's fork has 5 changes worth incorporating. This plan organizes ALL remaining work into a strict no-regression sequence: each phase is self-contained, testable, and must pass verification before the next phase begins.

**Current state:** 4-ch PWM, IMU, Baro, Mag, GPS, Battery ADC, Safety, SD, USB, RC serial, HRT, QSPI flash, HITL verified. PID gains zeroed. Debug flags causing CPU overload.

**Critical findings from deeper review (9 additional issues):**
1. sam_hsmci_initialize(0,0) crashes — driver uses cdcfg/cdirq unconditionally (lines 209, 264-266)
2. Disabling UART0 renumbers ttyS* — breaks GPS1/RC serial mappings in default.px4board
3. GPS config is UART2 (PD25/PD26) but comments say PD15/PD16 (USART2) — needs hw verification
4. ICM45686 enabled but has no SPI bus entry in spi.cpp — driver silently fails
5. GPIO IRQ passes pinset instead of IRQ number (confirmed)
6. PROGMEM/linker overlap confirmed
7. board_critmon uses wrong function names (board_critmon_* vs up_critmon_*)
8. SD init has 1000ms fixed delay + unconditional return OK masking failures
9. CAN pin docs wrong — MCAN0 is PB3/PB2, NOT PD28; PD28 is MCAN1_RX_1

---

## Phase 0: Immediate Safety & Cleanup (Est. 3-4 hours)

*Goal: Make the board safe for flight testing and fix low-hanging bugs.*

### Step 0.1 — Remove Debug Flags (Issue A7)
- **File:** `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- **Action:** Set to `n` or remove:
  - `CONFIG_DEBUG_FS_INFO=y` → `# CONFIG_DEBUG_FS_INFO is not set`
  - `CONFIG_DEBUG_MEMCARD_INFO=y` → `# CONFIG_DEBUG_MEMCARD_INFO is not set`
  - `CONFIG_SAMV7_HSMCI_CMDDEBUG=y` → `# CONFIG_SAMV7_HSMCI_CMDDEBUG is not set`
  - `CONFIG_DEBUG_INFO=y` → `# CONFIG_DEBUG_INFO is not set`
- **Verification:** Build, boot, confirm SD write latency < 5ms (was 40-80ms), logger CPU < 10%

### Step 0.2 — Disable UART0 + Fix Serial Renumbering (Issue A3, New Issue 2)
- **DANGER: Disabling UART0 shifts ALL ttyS numbers. Must update mappings atomically.**
- **Current ttyS assignment** (USART1=console, UART0, UART2, UART4):
  - ttyS0 = USART1 (console), ttyS1 = UART0 (unused), ttyS2 = UART2 (GPS), ttyS3 = UART4 (RC)
- **After UART0 disabled:**
  - ttyS0 = USART1 (console), ttyS1 = UART2 (GPS), ttyS2 = UART4 (RC)
- **Files to change atomically:**
  1. `nuttx-config/nsh/defconfig`: `CONFIG_SAMV7_UART0=y` → `# CONFIG_SAMV7_UART0 is not set`
  2. `default.px4board`: Update serial mappings:
     - `CONFIG_BOARD_SERIAL_GPS1="/dev/ttyS2"` → `CONFIG_BOARD_SERIAL_GPS1="/dev/ttyS1"`
     - `CONFIG_BOARD_SERIAL_RC="/dev/ttyS3"` → `CONFIG_BOARD_SERIAL_RC="/dev/ttyS2"`
     - `CONFIG_BOARD_SERIAL_TEL2="/dev/ttyS1"` → **REMOVE** (was pointing to UART0, no physical port)
  3. `init/rc.board_defaults`: Update comment `# GPS on UART2 = /dev/ttyS2` → `# GPS on UART2 = /dev/ttyS1`
- **Why:** PA9 (Safety) + PB0 (Motor 4) conflict with UART0 RXD/TXD. UART0 is never used.
- **Verification:** Build, boot, `ls /dev/ttyS*` shows ttyS0-ttyS2 only (no phantom ttyS3 from UART0), RC works on ttyS2 (`listener input_rc`). GPS functional test deferred to Step 2.0.

### Step 0.3 — Fix UART4 for SBUS (from Rathi)
- **File:** `nuttx-config/nsh/defconfig`
- **Action:** Change UART4 config:
  - `CONFIG_UART4_BAUD=115200` → `CONFIG_UART4_BAUD=100000`
  - `CONFIG_UART4_PARITY=0` → `CONFIG_UART4_PARITY=2` (even)
  - `CONFIG_UART4_2STOP=0` → `CONFIG_UART4_2STOP=1` (2 stop bits)
- **Why:** SBUS protocol is 100000 baud, 8E2. Correct defconfig values prevent early-boot framing errors.
- **Verification:** Build, boot, confirm RC SBUS input still works (`listener input_rc`)

### Step 0.4 — Fix SD Card Detect Pin Conflict (Issues B4, New Issue 1, from Rathi)
- **DANGER: Simply passing 0,0 to sam_hsmci_initialize() will crash.** The driver at `sam_hsmci.c:209` calls `sam_configgpio(state->cdcfg)` unconditionally, and lines 264-266 do `sam_gpioirq(state->cdcfg)`, `irq_attach(state->cdirq, ...)`, `sam_gpioirqenable(state->cdirq)` — all unconditionally. Passing cdcfg=0 configures a garbage GPIO; passing cdirq=0 attaches a handler to IRQ 0.
- **Files (all changes required together):**
  1. `src/board_config.h` — Comment out `GPIO_HSMCI0_CD` and `IRQ_HSMCI0_CD` defines
  2. `src/init.c` — Change to `sam_hsmci_initialize(HSMCI0_SLOTNO, HSMCI0_MINOR, 0, 0)`
  3. `src/sam_hsmci.c` — **Guard the CD GPIO/IRQ code:**
     - Line 209: Wrap `sam_configgpio(state->cdcfg)` in `if (state->cdcfg != 0)`
     - Lines 264-266: Wrap `sam_gpioirq()`, `irq_attach()`, `sam_gpioirqenable()` in `if (state->cdcfg != 0 && state->cdirq != 0)`
     - `sam_cardinserted_internal()`: If `state->cdcfg == 0`, return `true` (card always present)
     - Remove all debug `printf()` statements from this file
  4. `nuttx-config/nsh/defconfig` — `CONFIG_MMCSD_HAVE_CARDDETECT=y` → `# CONFIG_MMCSD_HAVE_CARDDETECT is not set`
- **Why:** PD18 shared between SD CD and UART4 (RC SBUS). Must disable CD properly without crashing.
- **Verification:** Build, boot, SD mounts, RC works, no crash at init

### Step 0.5 — Fix I2C Sensor Bus Flag (from Rathi)
- **File:** `init/rc.board_sensors`
- **Action:** Change all I2C sensor start commands from `-I` (Internal) to `-X` (External):
  - `bmm150 start -I -b 1` → `bmm150 start -X -b 1`
  - `ak09916 start -I -b 1` → `ak09916 start -X -b 1` (if present)
  - `dps310 start -I -b 1` → `dps310 start -X -b 1` (if present)
  - `bmi088_i2c start -A -I -b 1` → `bmi088_i2c start -A -X -b 1`
  - `bmi088_i2c start -G -I -b 1` → `bmi088_i2c start -G -X -b 1`
- **Why:** `i2c.cpp` declares `initI2CBusExternal(1)`. Using `-I` (Internal) selects a non-existent bus, causing sensors to silently fail. Rathi's `-X` flag is correct.
- **Verification:** Build, boot, `listener sensor_mag` to confirm BMM150 data flowing

### Step 0.6 — Fix SPI Bus Locking (New Issue — shared bus safety)
- **File:** `boards/microchip/samv71-xult-clickboards/src/spi.cpp`
- **Action:** Change `.requires_locking = false` → `.requires_locking = true` (line ~62)
- **Why:** SPI0 is shared between multiple devices (IMU + BMP388). When `requires_locking=false`, `src/lib/drivers/device/nuttx/SPI.cpp:69` sets `_locking_mode = LOCK_NONE`, bypassing the SPI bus mutex entirely. If drivers run on different work queues (e.g., IMU on SPI wq, BMP388 on sensor wq), concurrent SPI transactions will corrupt each other. Even with a single device today, this is a correctness fix — adding any second SPI device later would silently regress.
- **Verification:** Build, boot, confirm SPI sensors still work (may see slightly higher latency from mutex, but no data corruption)

### ~~Step 0.6b~~ — Add ICM45686 SPI Bus Entry — **DEFERRED to Step 2.0**
- **BLOCKED BY:** Step 2.0 (GPS/IMU pin conflict decision). The ICM45686 CS pin cannot be chosen until we know whether Path A (ICM45686 on PD27, remove ICM20689) or Path B (keep ICM20689, move GPS) is selected.
- **What was planned:** Add `DRV_IMU_DEVTYPE_ICM45686` to `spi.cpp` + `GPIO_SPI0_CS_ICM45686` to `board_config.h`
- **Now handled as part of Step 2.0** — each Path (A/B/C) includes the correct SPI table update with the confirmed CS pin.

### Step 0.7 — Enable I2C Glitch Filter (from Rathi, optional)
- **File:** `nuttx-config/nsh/defconfig`
- **Action:** Change `CONFIG_SAMV7_TWIHS0_GLITCH_FILTER=0` → `CONFIG_SAMV7_TWIHS0_GLITCH_FILTER=1`
- **Verification:** I2C sensors still work reliably

### Step 0.8 — Add PWM Defaults to rc.board_defaults (from Rathi, selective)
- **File:** `init/rc.board_defaults`
- **Action:** Add these standard defaults (keep existing HITL params):
  ```
  param set-default PWM_MAIN_FUNC1 101
  param set-default PWM_MAIN_FUNC2 102
  param set-default PWM_MAIN_FUNC3 103
  param set-default PWM_MAIN_FUNC4 104
  param set-default PWM_MAIN_MIN1 1100
  param set-default PWM_MAIN_MIN2 1100
  param set-default PWM_MAIN_MIN3 1100
  param set-default PWM_MAIN_MIN4 1100
  param set-default PWM_MAIN_MAX1 1900
  param set-default PWM_MAIN_MAX2 1900
  param set-default PWM_MAIN_MAX3 1900
  param set-default PWM_MAIN_MAX4 1900
  param set-default PWM_MAIN_DIS1 1000
  param set-default PWM_MAIN_DIS2 1000
  param set-default PWM_MAIN_DIS3 1000
  param set-default PWM_MAIN_DIS4 1000
  param set-default PWM_MAIN_FAIL1 1000
  param set-default PWM_MAIN_FAIL2 1000
  param set-default PWM_MAIN_FAIL3 1000
  param set-default PWM_MAIN_FAIL4 1000
  param set-default PWM_MAIN_TIM0 400
  ```
- **Also add** arming convenience params:
  ```
  param set-default COM_ARM_SDCARD 0
  param set-default COM_ARM_CHK_ESCS 0
  param set-default MAV_PROTO_VER 2
  ```
- **DO NOT incorporate** from Rathi: baked-in calibration data, RC calibration, PID values, `COM_RC_IN_MODE 3`, motor M1/M4 swap
- **Verification:** Build, boot, `param show PWM_MAIN_FUNC1` shows 101

### Phase 0 Gate
- [ ] Clean build with `make clean && make microchip_samv71-xult-clickboards_default`
- [ ] Boot with no hardfaults
- [ ] `ls /dev/ttyS*` shows correct number of devices (no phantom UART0)
- [ ] RC input works on new ttyS path (`listener input_rc` shows channel data)
- [ ] GPS ttyS path updated in default.px4board (functional test deferred to Phase 2 Step 2.0 — GPS/IMU pin conflict must be resolved first)
- [ ] SD card mounts (no crash at init), all 4 motors respond to `pwm test`
- [ ] SD write latency < 5ms
- [ ] Safety button functional
- [ ] I2C sensors report data (`listener sensor_mag`)
- [ ] SPI bus locking enabled (Step 0.6) — no concurrent SPI corruption

---

## Phase 1: Merge Blockers & Code Hygiene (Est. 1-2 hours)

*Goal: Make the branch mergeable to main without breaking other boards.*

### Step 1.1 — Revert ALL rcS Changes (Issue B1) — CRITICAL
- **File:** `ROMFS/px4fmu_common/init.d/rcS`
- **Full diff scope** (verified via `git diff upstream/main -- ROMFS/...`): diff is **larger than originally claimed**. Beyond the 4 board-specific disables, it also:
  1. SD auto-format disabled (lines 74-91) — replaced with warning message
  2. Dataman start commented out (lines 276-285)
  3. Navigator start commented out (line 542)
  4. `payload_deliverer start` commented out (line 585)
  5. Stale "RE-ENABLED" comments added at lines 50, 513, 516, 519, 687
  6. **UAVCAN/Cyphal block removed from original location** (after pwm_out) and **re-added in wrong location** (after bootloader upgrade) — changes startup ordering for ALL boards
  7. **Zenoh start block removed entirely** — breaks Zenoh-enabled boards
  8. **rc.vtxtable sourcing removed** — breaks VTX table loading for FPV boards
- **Action:** Revert against **upstream/main** (not local main, which has its own divergence):
  ```sh
  git checkout upstream/main -- ROMFS/px4fmu_common/init.d/rcS
  ```
  **IMPORTANT:** Local `main` branch has unrelated commits (`docs: Update main branch README...`) and may itself differ from true PX4 upstream. Always use `upstream/main` as the canonical source.
- **Why:** These affect ALL PX4 boards if merged to main. Board-specific gating belongs in board scripts only.
- **Verification:** `git diff upstream/main -- ROMFS/px4fmu_common/` shows zero changes

### Step 1.2 — Keep Board-Level Dataman Start on QSPI (Issue B5) — CRITICAL
- **File:** `init/rc.board_defaults`
- **Action:** **KEEP** `dataman start -f /fs/mtd_waypoints` in `rc.board_defaults`. Do NOT remove it.
- **Why this is critical:** After reverting rcS (Step 1.1), the common rcS will call `dataman start` (no `-f` flag). Without `-f`, dataman defaults to `PX4_STORAGEDIR "/dataman"` (i.e., `/fs/microsd/dataman` — an SD card file), NOT the QSPI waypoint partition (`dataman.cpp:158,987`). The board-level `dataman start -f /fs/mtd_waypoints` must run **before** the common rcS dataman block to ensure QSPI is used.
- **Ordering concern:** `rc.board_defaults` runs before common rcS, so board-level dataman starts first. When common rcS then calls `dataman start`, dataman is already running and the second start is a no-op. This is the correct behavior.
- **Guard (optional, for robustness):**
  ```sh
  # Start dataman on QSPI waypoints partition (must run before common rcS dataman)
  if [ -e /fs/mtd_waypoints ]; then
      dataman start -f /fs/mtd_waypoints
  else
      echo "WARN [init] QSPI waypoints partition not found, dataman will use SD fallback"
  fi
  ```
- **Impact if removed:** Mission/geofence/rally point persistence silently moves to SD card. If SD is absent or fails, dataman falls back to RAM backend and all waypoints are lost on reboot.
- **Verification:** Boot, check startup log (`dmesg | grep dataman`) for the opened file path. Dataman logs `"Could not open data manager file %s"` on failure, and on success the path is visible in the open call sequence. Confirm path is `/fs/mtd_waypoints` (QSPI), NOT `/fs/microsd/dataman` (SD fallback). `dataman status` confirms running state but does not reliably show the backend path.

### Step 1.3 — Remove Board-Level Navigator Override (NEW — double-start prevention)
- **File:** `init/rc.board_extras`
- **Current content (line 11):** `navigator start` — started unconditionally
- **Action:** Remove `navigator start` from `rc.board_extras`. After reverting rcS (Step 1.1), the common rcS will start navigator at its normal position. Keeping the board-level start causes navigator to start twice (once from `rc.board_extras`, once from common rcS).
- **Why it exists:** Was added as a workaround when common rcS navigator was commented out. With rcS reverted, the workaround becomes a bug.
- **Verification:** Boot, `navigator status` shows running (started once). Check `dmesg` for no "already running" warnings.

### Step 1.4 — Fix Misleading Comment (Issue A2)
- **File:** `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c`
- **Action:** Fix comment at line ~391:
  - Old: `/* CDTY=0 means output stays high (with CPOL=1) — safe for ESCs */`
  - New: `/* CDTY=0 with CPOL=1 = 0% duty cycle (output LOW entire period) — safe for ESCs */`
- **Verification:** Build succeeds (comment-only change)

### Step 1.5 — Split HITL vs Flight Defaults (NEW — safety defaults)
- **File:** `init/rc.board_defaults`
- **Current state (lines 23-28):** Permissive circuit breakers are set unconditionally:
  ```
  param set-default CBRK_SUPPLY_CHK 894281   # Bypasses power supply check
  param set-default COM_ARM_WO_GPS 1         # Arms without GPS
  param set-default CBRK_FLIGHTTERM 121212   # Disables flight termination
  ```
  These are necessary for HITL (SYS_AUTOSTART=1001) but dangerous for real flight (SYS_AUTOSTART=4001).
- **Action:** Gate permissive breakers on HITL airframe, with explicit safe-reset for non-HITL:
  ```sh
  # Always-safe defaults
  param set-default SYS_HAS_MAG 1
  param set-default SYS_HAS_BARO 1

  # HITL vs Flight mode circuit breakers
  # IMPORTANT: Both branches must be present. param set-default only sets values
  # if the param has never been explicitly changed. But if a user runs HITL first,
  # the breakers get persisted. The non-HITL branch ensures they are reset.
  if param compare SYS_AUTOSTART 1001
  then
      # HITL mode: bypass checks that can't pass in simulation
      param set-default CBRK_SUPPLY_CHK 894281
      param set-default CBRK_FLIGHTTERM 121212
      param set-default HIL_ACT_FUNC1 101
      param set-default HIL_ACT_FUNC2 102
      param set-default HIL_ACT_FUNC3 103
      param set-default HIL_ACT_FUNC4 104
  else
      # Flight mode: forcibly clear any persisted HITL breaker values.
      # MUST use "param reset", NOT "param set-default":
      #   param set-default only changes the runtime-default layer
      #   (parameters.cpp:594 → runtime_defaults.store()). If a user
      #   previously ran HITL and the breaker got saved to user_config
      #   (e.g. via QGC param save or "param set"), set-default cannot
      #   override it — user_config takes priority over runtime_defaults.
      #   param reset (parameters.cpp:670 → param_reset_internal()) removes
      #   the param from user_config entirely, causing param_get() to fall
      #   back to the firmware default (0 for both breakers).
      param reset CBRK_SUPPLY_CHK
      param reset CBRK_FLIGHTTERM
  fi

  # Allow arming without GPS on dev board (safe for both modes)
  param set-default COM_ARM_WO_GPS 1
  ```
- **Why `param reset` (not `param set-default`) in else branch:** `param set-default` only modifies the runtime-default layer (`parameters.cpp:594`). If HITL breaker values were persisted to user_config (flash) — via QGC, `param set`, or `param save` — `param set-default` cannot override them. `param reset` (`parameters.cpp:670`) removes the param from user_config entirely, so `param_get()` falls back to the firmware default (0). This is the ONLY reliable way to clear sticky persisted values from shell scripts.
- **Verification:** Set `SYS_AUTOSTART 1001`, reboot, confirm breakers are set. Switch to `SYS_AUTOSTART 4001`, reboot, confirm `CBRK_SUPPLY_CHK` is 0 and `CBRK_FLIGHTTERM` is 0.

### Phase 1 Gate
- [ ] `git diff upstream/main -- ROMFS/px4fmu_common/` shows no changes to shared files
- [ ] Board boots and all Phase 0 verifications still pass
- [ ] Dataman uses QSPI backend (`dmesg | grep dataman` shows `/fs/mtd_waypoints` path)
- [ ] Navigator starts once (no double-start from board_extras)
- [ ] HITL breakers only active when SYS_AUTOSTART=1001

---

## Phase 2: Core Infrastructure Fixes (Est. 3-5 days)

*Goal: Fix the foundational platform bugs that block higher-level features.*

### Step 2.0 — Resolve GPS UART2 vs IMU Pin Conflict (Issue B3, New Issue 3) — DECISION GATE
- **Problem:** `defconfig` enables `CONFIG_SAMV7_UART2=y` which maps to PD25(RXD)/PD26(TXD).
  PD25 is also `GPIO_SPI0_CS_ICM20689`. GPS and ICM20689 **cannot coexist** on UART2.
  Board comments reference PD15/PD16 (which are USART2 — a different peripheral entirely).
- **Rathi's approach:** Removed ICM20689, used ICM45686 on PD27 instead, freeing PD25 for GPS UART2.
  But also defined BMP388 CS on PD27 (same pin!) — works only because BMP388 runs via I2C.
- **ACTION: Check which IMU Click is physically plugged in, then follow the matching path:**

**Path A — ICM45686 on EXT2 (Rathi-style, GPS stays on UART2):**
  1. Remove ICM20689 from `spi.cpp` and `board_config.h` (PD25 freed)
  2. Add ICM45686 to `spi.cpp` + `board_config.h` **(deferred from Step 0.6b)**:
     - `board_config.h`: `#define GPIO_SPI0_CS_ICM45686 (GPIO_OUTPUT|GPIO_OUTPUT_SET|GPIO_PORT_PIOD|GPIO_PIN27)`
     - `spi.cpp`: `make_spidev(DRV_IMU_DEVTYPE_ICM45686, GPIO_SPI0_CS_ICM45686, 0)`
     - **Note:** Rathi's PD27 bug (BMP388 CS also on PD27) is fixed by removing BMP388 SPI CS entirely (step 5)
  3. Remove ICM20689 from `rc.board_sensors` and `default.px4board`
  4. Move BMP388 to I2C mode (`bmp388 -X -b 1 start`) since PD27 is now ICM45686 CS
  5. Remove stale `GPIO_SPI0_CS_BMP388` define (PD27 conflict with ICM45686)
  6. GPS stays on UART2 (PD25/PD26) — no ttyS changes beyond Step 0.2
  7. ttyS map after UART0 disabled: ttyS0=USART1(console), ttyS1=UART2(GPS), ttyS2=UART4(RC)

**Path B — ICM20689 on EXT1 (GPS must move off PD25):**
  Option B1: Switch GPS to USART2 (PD15/PD16 — no pin conflict):
    - `defconfig`: `CONFIG_SAMV7_UART2=y` → `# CONFIG_SAMV7_UART2 is not set`
    - Add: `CONFIG_SAMV7_USART2=y`, `CONFIG_USART2_SERIALDRIVER=y`, `CONFIG_USART2_BAUD=57600`
    - **WARNING:** USART2_RTS = PD18 (UART4 RC conflict) — do NOT enable flow control
    - Recalculate ttyS map (USART2 slots in after USARTs in priority cascade)
    - Wire GPS to PD15(RX)/PD16(TX) physically
  Option B2: Keep both on UART2 but move ICM20689 CS to a different GPIO with jumper wire
  Option B3: Disable GPS entirely for now — simplest if GPS hardware isn't connected yet

**Path C — No GPS connected yet:**
  1. Disable UART2: `# CONFIG_SAMV7_UART2 is not set`
  2. Remove GPS serial mapping from `default.px4board`
  3. ttyS map: ttyS0=USART1(console), ttyS1=UART4(RC) — only 2 serial ports
  4. `default.px4board`: `CONFIG_BOARD_SERIAL_RC="/dev/ttyS1"`
  5. Defer GPS decision to custom PCB phase

- **Note:** Step 0.2 ttyS mappings MUST be recalculated after this decision. Do Steps 0.2 and 2.0 together.
- **Verification:** `gps status` (if GPS connected) or `ls /dev/ttyS*` confirms correct port count

### Step 2.1 — Fix GPIO Interrupt Backend (Issue A1, New Issue 5) — HIGH PRIORITY
- **File:** `boards/microchip/samv71-xult-clickboards/src/sam_gpiosetevent.c`
- **Current bugs (confirmed):**
  - Line 109: `sam_gpioirqenable(intcfg)` passes a `gpio_pinset_t` but the function signature is `void sam_gpioirqenable(int irq)` (`sam_gpio.h:383`) — wrong argument type, silent no-op
  - Line 103: `sam_gpioirq(intcfg)` configures the PIO controller for interrupts (correct)
  - Missing: `irq_attach()` is never called — the handler callback is received but discarded
  - Missing: No mapping from pinset → virtual IRQ number
- **CRITICAL DESIGN FIX: Do NOT create custom port-level ISR dispatchers.**
  NuttX ALREADY installs port-level handlers in `sam_gpioirqinitialize()` (`sam_gpioirq.c:231-267`):
  - `sam_gpioainterrupt` → reads PIO_ISR → `irq_dispatch(SAM_IRQ_PA0 + pin)` for each set bit
  - Same for PIOB/C/D/E
  Re-attaching port IRQs from board code would overwrite NuttX's dispatchers and break all GPIO IRQs.
- **Correct approach — work WITH NuttX's existing dispatch:**
  ```c
  // Helper: convert pinset to virtual IRQ number.
  // Uses per-port base constants (SAM_IRQ_PA0, SAM_IRQ_PB0, ...) + pin offset.
  // Mirrors the structure of sam_irqbase() in sam_gpioirq.c:97-142.
  // GPIO_PORT_PIOA=0, PIOB=1, PIOC=2, PIOD=3, PIOE=4 (sam_gpio.h:122-126)
  //
  // IMPORTANT: SAM_IRQ_PCx/PEx are only defined when CONFIG_SAMV7_GPIOx_IRQ
  // is enabled (samv71_irq.h:313,393). Current defconfig only enables A/B/D.
  // Using unguarded SAM_IRQ_PC0 or SAM_IRQ_PE0 causes a compile error.
  static int pinset_to_virq(gpio_pinset_t pinset) {
      int pin  = (pinset & GPIO_PIN_MASK) >> GPIO_PIN_SHIFT;
      int port = (pinset & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT;

      switch (port) {
#ifdef CONFIG_SAMV7_GPIOA_IRQ
      case 0: return SAM_IRQ_PA0 + pin;  // PIOA
#endif
#ifdef CONFIG_SAMV7_GPIOB_IRQ
      case 1: return SAM_IRQ_PB0 + pin;  // PIOB
#endif
#ifdef CONFIG_SAMV7_GPIOC_IRQ
      case 2: return SAM_IRQ_PC0 + pin;  // PIOC
#endif
#ifdef CONFIG_SAMV7_GPIOD_IRQ
      case 3: return SAM_IRQ_PD0 + pin;  // PIOD
#endif
#ifdef CONFIG_SAMV7_GPIOE_IRQ
      case 4: return SAM_IRQ_PE0 + pin;  // PIOE
#endif
      default: return -EINVAL;
      }
  }

  int sam_gpiosetevent(gpio_pinset_t pinset, bool risingedge, bool fallingedge,
                       bool event, xcpt_t handler, void *arg) {
      gpio_pinset_t intcfg;

      // 1. Build interrupt config — keep existing edge-selection logic verbatim
      //    (sam_gpiosetevent.c lines 72-91, already correct)
      if (event) {
          intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;
      } else if (risingedge && fallingedge) {
          intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_BOTHEDGES;
      } else if (risingedge) {
          intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_RISING;
      } else if (fallingedge) {
          intcfg = (pinset & ~GPIO_INT_MASK) | GPIO_INT_FALLING;
      } else {
          return -EINVAL;
      }
      intcfg = (intcfg & ~GPIO_MODE_MASK) | GPIO_INPUT;

      // 2. Convert pinset to virtual IRQ number
      int virq = pinset_to_virq(pinset);
      if (virq < 0) return virq;

      // 3. Configure the GPIO pin for interrupt
      sam_configgpio(intcfg);
      sam_gpioirq(intcfg);

      if (handler) {
          // 4. Attach handler to NuttX's per-pin virtual IRQ
          //    NuttX's existing port dispatcher (sam_gpioirq.c) reads PIO_ISR
          //    and calls irq_dispatch(virq) for each set bit — our handler
          //    is invoked through that dispatch chain.
          irq_attach(virq, handler, arg);
          sam_gpioirqenable(virq);  // Correct arg type: int irq (not pinset)
      } else {
          sam_gpioirqdisable(virq);
          irq_attach(virq, NULL, NULL);
      }
      return OK;
  }
  ```
  **No dispatch table needed. No port-level ISR attachment.** NuttX handles all of that.
- **Verification:**
  - Build succeeds
  - Boot, ICM20689 uses DRDY interrupt (not polling fallback): `listener sensor_accel` shows consistent ~1kHz rate
  - `perf` shows DRDY ISR counter incrementing

### Step 2.2 — Fix micro_hal.h Undefined Symbols (Issue B6, New Issue 3b)
- **File:** `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h`
- **3 undefined symbols** (confirmed — `grep` for `GPIO_INT[^_]` in `sam_gpio.h` returns no matches):
  - Line 113 `PX4_MAKE_GPIO_EXTI`: Uses bare `GPIO_INT` (undefined) and `GPIO_PULLUP` (undefined)
  - Line 117 `PX4_GPIO_PIN_OFF`: Uses `GPIO_FLOAT` (undefined)
- **Action:**
  - `GPIO_INT` → `GPIO_INT_BOTHEDGES` (or omit — edge type is set by `sam_gpiosetevent()` at runtime)
  - `GPIO_PULLUP` → `GPIO_CFG_PULLUP`
  - `GPIO_FLOAT` → `GPIO_CFG_DEFAULT`
  - Fixed macros:
    ```c
    #define PX4_MAKE_GPIO_EXTI(gpio) \
        (((gpio) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INT_BOTHEDGES|GPIO_INPUT|GPIO_CFG_PULLUP))
    #define PX4_GPIO_PIN_OFF(def) \
        (((def) & (GPIO_PORT_MASK | GPIO_PIN_MASK)) | (GPIO_INPUT|GPIO_CFG_DEFAULT))
    ```
- **Why these are latent bugs:** These macros are never instantiated in the current build (no PPS, RPM drivers enabled), so no compile error yet. But enabling any driver that uses `PX4_MAKE_GPIO_EXTI()` would fail.
- **Verification:** Build succeeds; temporarily enable `CONFIG_DRIVERS_PPS_CAPTURE=y` to confirm no compile error, then disable

### Step 2.3 — Fix Board UUID Using True 128-bit Unique ID (Issue A4) — Full scope
- **File:** `platforms/nuttx/src/px4/microchip/samv7/version/board_identity.c`
- **Problem:** Three functions fabricate pseudo-unique data by XOR-ing the 2-word CIDR/EXID with index-shifted values (`chip_uuid[i % 2] ^ (i << 24)` at lines 94, 131, 174). CIDR/EXID are chip-family identifiers, NOT per-chip unique — all SAMV71Q21B chips with the same stepping have identical values. Zero-filling words 2-3 would make all boards identical.
- **Solution:** SAMV71 **does** have a true 128-bit per-chip unique ID, accessible via NuttX's `sam_get_uniqueid()` (`sam_uid.c:50`). It reads the Unique Identifier via EEFC Start Read (STUI) / Stop Read (SPUI) flash commands. This returns 4 genuinely unique 32-bit words per chip.
- **Prerequisites (3 items, all required):**
  1. **Enable in defconfig:**
     ```
     CONFIG_BOARDCTL_UNIQUEID=y
     CONFIG_BOARDCTL_UNIQUEID_SIZE=16
     ```
  2. **Implement `board_uniqueid()`** — `boardctl.c:416` calls `board_uniqueid()` when
     `CONFIG_BOARDCTL_UNIQUEID` is enabled. No implementation exists in our board tree.
     Add to `board_identity.c` (or a new file compiled into the board BSP), following
     the upstream NuttX samv71-xult pattern (`sam_appinit.c:93`):
     ```c
     #include "sam_uid.h"
     #ifdef CONFIG_BOARDCTL_UNIQUEID
     int board_uniqueid(FAR uint8_t *uniqueid)
     {
         if (uniqueid == NULL) { return -EINVAL; }
         sam_get_uniqueid(uniqueid);
         return OK;
     }
     #endif
     ```
     Without this, enabling `CONFIG_BOARDCTL_UNIQUEID` causes a **link error**.
  3. **UUID byte length decision:** `micro_hal.h:60` defines `PX4_CPU_UUID_BYTE_LENGTH=12`
     (3 × 32-bit words). SAMV7's true UID is 128 bits (16 bytes, 4 words). Changing to 16
     would ripple through `uuid_uint32_t`, format strings, `board_get_mfguid()` buffer size,
     and MAVLink `uid2` field (which is already 18 bytes).
     **Decision: Keep 12 bytes.** Use the first 12 bytes (words 0-2) of the 16-byte UID.
     These are already unique per chip — truncation is safe. This preserves compatibility
     with all PX4 UUID consumers. Document that words 0-2 are used, word 3 is available
     if 16-byte support is added later.
- **Action — rewrite all three functions to use `sam_get_uniqueid()`:**
  1. **`board_get_uuid32()`** — Replace CIDR/EXID+XOR with true UID (first 12 bytes):
     ```c
     #include "sam_uid.h"
     void board_get_uuid32(uuid_uint32_t uuid_words) {
         uint8_t uniqueid[16];
         sam_get_uniqueid(uniqueid);
         /* Use first 12 bytes (3 words) — PX4_CPU_UUID_BYTE_LENGTH is 12 */
         memcpy(uuid_words, uniqueid, PX4_CPU_UUID_BYTE_LENGTH);
     }
     ```
  2. **`board_get_mfguid()`** (line 120) — Same approach: call `sam_get_uniqueid()`, copy
     first `PX4_CPU_MFGUID_BYTE_LENGTH` (12) bytes. Remove the `chip_uuid[i % 2] ^ (i << 24)` fabrication loop.
  3. **`board_get_px4_guid()`** (line 166) — Same approach: `sam_get_uniqueid()` then copy
     first `PX4_CPU_UUID_WORD32_LENGTH` (3) words after soc_arch_id prefix. The existing
     `PX4_GUID_BYTE_LENGTH - sizeof(soc_arch_id) - PX4_CPU_UUID_BYTE_LENGTH` zero-pad
     calculation (line 162) remains correct since both constants stay at 12. Remove XOR loop.
  4. **Remove** `SAMV7_UUID_BASE` (CHIPID_CIDR/EXID) usage entirely — no longer needed.
- **Why all three:** `board_get_uuid32()` feeds `ver hwcmp`; `board_get_mfguid()` feeds MAVLink AUTOPILOT_VERSION; `board_get_px4_guid()` feeds MAVLink SYS_STATUS. All must use the same true UID source.
- **Verification:** `ver hwcmp` and `ver all` show a unique ID that differs between physical boards. Two boards with same firmware show different UIDs. Confirm UUID is 12 bytes / 3 words (no format string overflow).

### Step 2.4 — Fix Board Reset Persistence (Issue A5)
- **File:** `platforms/nuttx/src/px4/microchip/samv7/board_reset/board_reset.cpp`
- **Scope:** Only `board_configure_reset()` and `board_reset()` exist in this file. There is no `board_get_reset_reason()` in the SAMV7 port — reading/clearing GPBR on boot is a bootloader-side concern (deferred to Step 4.3).
- **Action:** Replace `static uint32_t reset_mode_value` (RAM, lost on reset) with GPBR0 register write:
  ```cpp
  #include <nuttx/arch.h>  // for putreg32/getreg32

  #define SAMV7_GPBR_BASE      0x400E1890
  #define SAMV7_BOOT_MODE_REG  (SAMV7_GPBR_BASE + 0)  // GPBR0

  int board_configure_reset(reset_mode_e mode, uint32_t arg) {
      if (mode < arraySize(modes)) {
          arg = (mode == BOARD_RESET_MODE_CAN_BL) ? arg & ~0xff : 0;
          putreg32(modes[mode] | arg, SAMV7_BOOT_MODE_REG);
          return OK;
      }
      return -EINVAL;
  }
  ```
  Remove the `static uint32_t reset_mode_value` variable entirely.
- **Note:** GPBR survives system reset but NOT full power-off (VDDIO removal). Sufficient for reboot-to-bootloader workflow. The bootloader (Step 4.3) will read GPBR0 to decide whether to stay in BL mode or jump to app.
- **Verification:** `reboot -b` writes to GPBR0; verify with debugger or `devmem 0x400E1890` that value persists across `up_systemreset()`

### Step 2.5 — Implement board_critmon (Issue A8, New Issue 7)
- **File:** `platforms/nuttx/src/px4/microchip/samv7/board_critmon/board_critmon.c`
- **Current bugs:** Uses wrong function names — `board_critmon_init()` and `board_critmon_report()` — but NuttX `CONFIG_SCHED_CRITMONITOR` expects `up_critmon_gettime()` and `up_critmon_convert()`. The current stubs are dead code that never gets called.
- **Action:** Delete current stubs entirely. Replace with DWT cycle counter implementation that mirrors `arm_perf.c` (`arch/arm/src/armv7-m/arm_perf.c:43-76`) exactly:
  ```c
  // Use the SAME headers as arm_perf.c (same NuttX layer, same build include paths)
  #include <nuttx/arch.h>
  #include <nuttx/clock.h>
  #include "dwt.h"           // DWT_CTRL, DWT_CYCCNT, DWT_CTRL_CYCCNTENA_MASK
  #include "itm.h"           // ITM_LAR
  #include "nvic.h"          // NVIC_DEMCR, NVIC_DEMCR_TRCENA
  #include <arch/board/board.h>  // BOARD_CPU_FREQUENCY

  uint32_t up_critmon_gettime(void) {
      return getreg32(DWT_CYCCNT);
  }

  void up_critmon_convert(uint32_t elapsed, FAR struct timespec *ts) {
      // CRITICAL: BOARD_CPU_FREQUENCY (300 MHz), NOT BOARD_MCK_FREQUENCY (150 MHz)
      // DWT CYCCNT counts CPU clock cycles, not peripheral bus (MCK) cycles.
      uint32_t left;
      ts->tv_sec  = elapsed / BOARD_CPU_FREQUENCY;
      left        = elapsed - ts->tv_sec * BOARD_CPU_FREQUENCY;
      ts->tv_nsec = NSEC_PER_SEC * (uint64_t)left / BOARD_CPU_FREQUENCY;
  }
  ```
  Also enable DWT cycle counter at board init (in `init.c` or `board_app_initialize`).
  **Sequence matters** — follows `arm_perf.c:49-56` exactly:
  ```c
  #include "dwt.h"
  #include "itm.h"
  #include "nvic.h"

  // 1. Enable trace unit FIRST (DWT is only clocked when TRCENA=1)
  modifyreg32(NVIC_DEMCR, 0, NVIC_DEMCR_TRCENA);

  // 2. Unlock DWT/ITM write access
  putreg32(0xc5acce55, ITM_LAR);

  // 3. Enable the cycle counter
  modifyreg32(DWT_CTRL, 0, DWT_CTRL_CYCCNTENA_MASK);
  ```
- **defconfig:** Enable `CONFIG_SCHED_CRITMONITOR=y` (if not already)
- **Verification:** `perf` command works; critical section monitoring shows real timing data

### Step 2.6 — Fix Linker Script PROGMEM Reservation (Issue B7)
- **File:** `boards/microchip/samv71-xult-clickboards/nuttx-config/scripts/script.ld`
- **Action:** Change flash LENGTH:
  - Old: `flash (rx) : ORIGIN = 0x00400000, LENGTH = 2048K`
  - New: `flash (rx) : ORIGIN = 0x00400000, LENGTH = 1792K`
  - This excludes the last 256KB (2 x 128KB sectors) reserved by `CONFIG_SAMV7_PROGMEM_NSECTORS=2`
- **Verification:** Build succeeds; `arm-none-eabi-size` shows flash usage against 1792K limit

### Step 2.7 — Fix SD Init Path (Issue B2, New Issue 8)
- **File:** `boards/microchip/samv71-xult-clickboards/src/init.c`
- **Current problems (3 bugs in `samv71_sdcard_initialize()`):**
  1. Line 147: `up_mdelay(1000)` — 1 second fixed boot delay, wastes time
  2. Line 157: Early `mount()` — causes EBUSY when rcS tries to mount again, sets `STORAGE_AVAILABLE=no`
  3. Line 165: `return OK` even if mount fails — masks SD failures silently
- **Action:** Rewrite `samv71_sdcard_initialize()`:
  - Keep `sam_hsmci_initialize()` call (necessary for driver init)
  - Replace `up_mdelay(1000)` with a short poll loop (check for `/dev/mmcsd0` up to 500ms with 50ms intervals)
  - **Remove** the early `mount()` call entirely — let rcS handle mounting
  - Remove `mkdir("/fs", ...)` and `mkdir("/fs/microsd", ...)` — rcS creates these
  - Return the actual error code from `sam_hsmci_initialize()`, not `OK`
  - Remove all debug `printf()` statements
- **Verification:** Boot time reduced by ~1s, `mount` shows SD mounted once by rcS, `STORAGE_AVAILABLE` set correctly

### Step 2.8 — Document USB VBUS Limitation (NEW — MEDIUM)
- **File:** `boards/microchip/samv71-xult-clickboards/src/usb.c`
- **Current state (line 109):** `board_read_VBUS_state()` always returns 0 (VBUS present). This means `cdcacm_autostart` always believes USB is connected, even when unplugged.
- **Impact:** USB-autostart logic never detects disconnect. On dev board this is acceptable (always USB-powered), but on custom PCB with battery power this causes phantom MAVLink on USB.
- **Action for now:** Add a clear comment documenting the limitation and the fix path:
  ```c
  /* SAMV71-XULT dev board: No dedicated VBUS sense GPIO available.
   * Always reports VBUS present. This is correct for desk use (always USB-powered).
   *
   * Custom PCB: Wire a VBUS sense GPIO and implement actual detection:
   *   return px4_arch_gpioread(GPIO_OTGFS_VBUS) ? 0 : 1;
   * Also define BOARD_VBUS_SENSE_GPIO in board_config.h.
   */
  ```
- **Defer real fix to:** Phase 5 (Custom PCB) where a dedicated VBUS sense pin will be available.
- **Verification:** Comment-only change; build succeeds.

### Phase 2 Gate
- [ ] All Phase 0/1 verifications still pass
- [ ] GPIO DRDY interrupts working (ICM20689 at full data rate)
- [ ] `ver hwcmp` and `ver all` show consistent UUID (no XOR fabrication)
- [ ] `reboot` preserves mode in GPBR
- [ ] `perf` shows real timing data
- [ ] Flash build stays within 1792K limit
- [ ] SD mounts cleanly without EBUSY

---

## Phase 3: Feature Implementations (Est. 2-4 weeks)

*Goal: Implement missing PX4 features to reach ~75% parity.*

### Step 3.1 — Enable Hardware Watchdog (1 day)
- **File:** `nuttx-config/nsh/defconfig`
- **Action:** Add:
  ```
  CONFIG_SAMV7_WDT=y
  CONFIG_WATCHDOG=y
  CONFIG_WATCHDOG_DEVPATH="/dev/watchdog0"
  ```
- **File:** `default.px4board`
- **Action:** Enable `CONFIG_DRIVERS_WATCHDOG=y` (PX4 watchdog module)
- **Verification:** `watchdog status` shows armed; system recovers from intentional hang test

### Step 3.2 — Implement Hardfault Logging (2-3 days)
- **Files:**
  - `src/board_config.h` — Add `#define HAS_PROGMEM 1`
  - `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h` — Replace `px4_savepanic` no-op:
    ```c
    // Old: #define px4_savepanic(fileno, context, length) (0)
    // New: include the proper header (platforms/nuttx/src/px4/common/include/px4_platform/progmem_dump.h)
    // which declares: int progmem_dump_savepanic(int fileno, uint8_t *context, int length);
    #include <px4_platform/progmem_dump.h>
    #define px4_savepanic(fileno, context, length) progmem_dump_savepanic(fileno, context, length)
    ```
    **Do NOT use ad-hoc forward declaration** — the proper header already exists at
    `platforms/nuttx/src/px4/common/include/px4_platform/progmem_dump.h:119` and also
    provides `progmem_dump_initialize()`, `struct progmem_s`, and IOCTL definitions needed
    by the hardfault_log system command.
  - `default.px4board` — Enable `CONFIG_SYSTEMCMDS_HARDFAULT_LOG=y`
- **Depends on:** Step 2.6 (linker script fix for PROGMEM)
- **Reference:** `platforms/nuttx/src/px4/common/board_crashdump.c` handles the crash capture; it auto-selects PROGMEM path when `HAS_PROGMEM` is defined
- **Verification:** Trigger a hardfault (e.g., null pointer dereference in NSH `perf` test), reboot, `hardfault_log` shows crash dump

### Step 3.3 — Fix OneShot Mode (Issue A6, 1 day)
- **File:** `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c`
- **Action:** In `io_timer_set_rate()`, add special handling for rate=0 (OneShot):
  ```c
  if (rate == 0) {
      // OneShot mode: configure for single-pulse triggering
      // Set CPRD to maximum, update duty only when trigger() is called
      return OK;
  }
  if (rate < 50 || rate > 8000) return -EINVAL;
  ```
- **File:** `platforms/nuttx/src/px4/microchip/samv7/io_pins/pwm_servo.c` — Already allows OneShot through
- **Verification:** `pwm rate -r 0` accepted; scope shows single pulses on trigger

### Step 3.4 — Implement Tone Alarm (2-3 days)
- **Files to create:**
  - `platforms/nuttx/src/px4/microchip/samv7/tone_alarm/ToneAlarmInterface.cpp`
  - `platforms/nuttx/src/px4/microchip/samv7/tone_alarm/CMakeLists.txt`
- **File to modify:** `src/board_config.h` — Add:
  ```c
  #define GPIO_TONE_ALARM_GPIO  /* Choose a free GPIO pin for buzzer */
  #define GPIO_TONE_ALARM_IDLE  (GPIO_OUTPUT|GPIO_CFG_DEFAULT|GPIO_OUTPUT_CLEAR|GPIO_PORT_PIOx|GPIO_PIN_y)
  ```
- **Approach:** Start with GPIO toggle mode (simplest, works with any piezo). Port the STM32 `ToneAlarmInterfaceGPIO.cpp` pattern. TC PWM mode is a future optimization.
- **Hardware required:** Wire a piezo buzzer to a free GPIO
- **Verification:** `tune_control play -t 1` plays the startup tune

### Step 3.5 — DShot Phase 2 (2-3 weeks)
*Follow the existing 9-step plan in `DSHOT_PHASE2_BUILD_PLAN.md` exactly.*

- **Step 3.5.1:** Scaffold `dshot.c` + CMake + board config → build gate
- **Step 3.5.2:** Scope test CPOL polarity + WRDY validation → **hard gate**
- **Step 3.5.3:** Fill `io_timer_set_dshot_mode()` in `io_timer_pwmc.c`
- **Step 3.5.4:** Fill `io_timer_update_dma_req()` + helpers
- **Step 3.5.5:** Implement `up_dshot_init()` in `dshot.c`
- **Step 3.5.6:** Implement `dshot_motor_data_set()` (packet encoding)
- **Step 3.5.7:** Implement `up_dshot_trigger()` (DMA + WRDY gate)
- **Step 3.5.8:** Implement `up_dshot_arm()` (enable/disable, force-low)
- **Step 3.5.9:** Bidirectional stubs return -ENOSYS

**Key files:**
- Create: `platforms/nuttx/src/px4/microchip/samv7/dshot/dshot.c`, `CMakeLists.txt`
- Modify: `io_timer_pwmc.c`, `io_timer.h`, `samv7/CMakeLists.txt`, `default.px4board`

**Verification per DShot plan:** scope verify each step, `dshot start`, `actuator_test`, ESC spin test

### Step 3.6 — Enable SD Logging (1 day)
- **File:** `init/rc.board_defaults`
- **Action:** Change `SDLOG_MODE -1` → `SDLOG_MODE 0` (enable logging to SD)
- **Also add:** `SDLOG_PROFILE 1`, `SDLOG_BACKEND 1`
- **Depends on:** Phase 0 (debug flags removed — without this, logging causes CPU overload)
- **Verification:** Arm, fly/HITL, check `/fs/microsd/log/` for ULog files

### Phase 3 Gate
- [ ] All Phase 0/1/2 verifications still pass
- [ ] Watchdog armed and recovering
- [ ] Hardfault dump captured and readable after induced crash
- [ ] OneShot PWM mode functional
- [ ] Tone alarm plays tunes
- [ ] DShot ESCs spinning (scope verified)
- [ ] ULog files written to SD

---

## Phase 4: Production Hardening (Est. 2-4 weeks)

*Goal: Reach production-ready state for the dev board.*

### Step 4.1 — RC Input Capture via TC5 (1 week)
- **Goal:** PPM/PWM RC input using Timer Counter 5 (TC1 CH2) on PC29
- **Current state:** Pin reserved (`GPIO_RC_INPUT` defined), no capture code
- **Approach:** Implement TC capture mode ISR, decode PPM frame timing
- **Note:** PD18 conflict with SD CD is already resolved (Phase 0.4)

### Step 4.2 — CAN/DroneCAN Validation (1-2 weeks)
- **Goal:** Validate MCAN0/MCAN1 silicon + enable DroneCAN stack
- **Files:** defconfig (enable `CONFIG_SAMV7_MCAN0=y`), board_config.h (MCAN pin defines)
- **Hardware required:** CAN transceiver module (e.g., MCP2562 Click board)
- **Pin mapping (corrected — New Issue 9):**
  - MCAN0: PB3 (RX) / PB2 (TX) — **NOT PD28** (previous docs were wrong)
  - MCAN1 option 1: PD28 (RX) / PD12 (TX) — PD28 conflicts with ICM20689 DRDY
  - MCAN1 option 2: PC12 (RX) / PC14 (TX) — no known conflicts
  - **Note:** PB3 is also USART0_RTS and PB2 is USART0_CTS — ensure USART0 is not enabled
  - Prefer MCAN0 (PB3/PB2) for dev board; MCAN1 on custom PCB

### Step 4.3 — PX4 Bootloader Port (2-3 weeks)
- **Files to create:**
  - `boards/microchip/samv71-xult-clickboards/bootloader.px4board`
  - `boards/microchip/samv71-xult-clickboards/nuttx-config/bootloader/defconfig`
  - `boards/microchip/samv71-xult-clickboards/src/bootloader_main.c`
  - `boards/microchip/samv71-xult-clickboards/nuttx-config/bootloader/script.ld`
- **Architecture:** Bootloader at flash start (0x00400000), reads GPBR0 for boot mode (depends on Step 2.4), jumps to application or stays for USB DFU
- **Reference:** STM32 FMUv6X bootloader pattern (`boards/px4/fmu-v6x/bootloader.px4board`)
- **Interim:** Document SAM-BA ROM bootloader procedure as fallback

### Step 4.4 — Additional Arming/Safety Params
- **File:** `init/rc.board_defaults`
- **Action:** Add reasonable failsafe defaults:
  ```
  param set-default NAV_RCL_ACT 2        # RC loss → Return (not terminate!)
  param set-default NAV_DLL_ACT 0        # Datalink loss → Hold
  param set-default COM_DL_LOSS_T 10     # Datalink loss timeout
  ```

### Phase 4 Gate
- [ ] PPM RC input decoded from TC5
- [ ] CAN bus communication verified with DroneCAN peripheral
- [ ] Bootloader flashes firmware over USB
- [ ] Failsafe behavior correct (RC loss → Return mode)

---

## Phase 5-7: Custom PCB (Future, Est. months)

*These require hardware changes and are out of scope for dev board work.*

### Phase 5: Custom PCB Design
- 8-channel PWM (add PWMC1 module)
- Dedicated VBUS sense GPIO (fixes Issue B8)
- CAN transceiver onboard — MCAN0 on PB3/PB2, MCAN1 on PC12/PC14 (avoids PD28 DRDY conflict)
- Second IMU + second baro for redundancy
- RGB LED outputs
- RSSI ADC channel
- User Signature UUID programming at manufacturing

### Phase 6: Power Management
- Power brick validation GPIOs
- Rail control (3.3V/5V enable pins)
- Heater control for IMU temperature stability

### Phase 7: Sensor Expansion
- Second SPI bus for redundant IMU
- Additional ADC channels (airspeed, analog RSSI)
- Camera trigger output
- Ethernet (GMAC + PHY)

---

## Changes NOT to Incorporate from Rathi

| Change | Reason |
|--------|--------|
| Motor M1/M4 swap (`timer_config.cpp`, `board_config.h`) | Conflicts with our verified HITL motor ordering |
| ICM-45686 CS = PD27 as-is | Rathi defines BOTH ICM45686 CS and BMP388 CS on PD27 — a bug. If Path A chosen, use PD27 for ICM45686 only and remove BMP388 SPI CS |
| `CONFIG_DRIVERS_RC=y` | This is NuttX IR remote driver, not PX4 RC input — wrong config |
| USB polling loop in `rc.board_mavlink` | Unnecessary 0-5s boot delay; our sercon works |
| Baked-in calibration data (`CAL_GYRO0_*`, etc.) | Board-specific, overrides valid stored calibration |
| Baked-in RC calibration (`RC1_MIN`, etc.) | TX-specific, not board defaults |
| PID values in defaults | Frame-specific tuning |
| `COM_RC_IN_MODE 3` | Disables real RC stick input in flight |
| Remove `HIL_ACT_FUNC1-4` | Breaks HITL mode |
| EVSE files (`main.c`, `ssd1309.*`) | Wrong project entirely |
| Rathi ttyS mapping table (CODEBASE_MERGE_CHECKLIST.md:263-268) | **Incorrect** — shows UART0 as ttyS0 but console USART1 gets ttyS0 first |

## Changes to Incorporate from Rathi (Summary)

| Change | Step | Status |
|--------|------|--------|
| UART4 SBUS config (100000/8E2) | Step 0.3 | Must incorporate |
| SD Card Detect disabled (PD18 freed) | Step 0.4 | Must incorporate (with guards) |
| I2C sensor `-I` → `-X` bus flag fix | Step 0.5 | Must incorporate |
| I2C glitch filter | Step 0.7 | Optional, low risk |
| PWM defaults, arming params, MAV_PROTO_VER | Step 0.8 | Selective incorporation |
| ICM20689→ICM45686 swap | Step 2.0 Path A | Conditional on IMU choice |
| BMP388 moved to I2C | Step 2.0 Path A | Conditional on IMU choice |

---

## Verification Strategy (No-Regression)

After each phase, run this checklist:

1. **Build:** `make microchip_samv71-xult-clickboards_default` — no errors, no new warnings
2. **Boot:** Board reaches NSH prompt within 10 seconds
3. **Sensors:** `listener sensor_accel`, `listener sensor_mag`, `listener sensor_baro` — all streaming
4. **PWM:** `pwm test -c 1234 -p 1200` — all 4 motors respond
5. **RC:** `listener input_rc` — shows channel values updating
6. **SD:** `ls /fs/microsd/` — accessible
7. **QSPI:** `param show SYS_AUTOSTART` — reads from flash
8. **USB MAVLink:** QGroundControl connects over USB
9. **HITL:** `SYS_AUTOSTART 1001`, jMAVSim connects, takeoff/hover/land works
10. **CPU:** `top` shows < 50% total CPU usage
