# SAMV71-XULT PX4 Production Gap Audit

Date: 2026-04-02

Baseline:
- Working board/port: `samv7-custom`, commit `be2a93b270` in `/media/bhanu1234/Development/PX4-Autopilot-Rathi`
- Flight status: flight-proven on 2026-03-25 for multicopter hover/RTL with GPS + RC
- Ground truth for what works: the Rathi repo
- NuttX SAMV7 arch review source: `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/...`

Reference boards compared:
- `boards/px4/fmu-v6x/`
- `boards/nxp/fmuk66-v3/`
- `boards/px4/fmu-v6xrt/`
- `platforms/nuttx/src/px4/stm/stm32_common/`
- `platforms/nuttx/src/px4/stm/stm32h7/`
- `platforms/nuttx/src/px4/nxp/kinetis/`
- `platforms/nuttx/src/px4/nxp/imxrt/`
- `platforms/nuttx/src/px4/nxp/rt117x/`

Status legend:
- `Yes`: present and usable in the flight-tested SAMV71 port
- `Partial`: present but incomplete, stubbed, disabled, or not production-grade
- `No`: missing in the SAMV71 PX4 port
- `N/A`: not relevant to the current SAMV71-XULT demo hardware

## Executive Summary

The SAMV71 port is already a real PX4 multicopter target, not a bring-up toy. Core flight control, SPI IMU, I2C mag/baro, PWM/OneShot125, GPS, SBUS, USB MAVLink, SD logging, QSPI parameter storage, HRT, and EKF2 all work on hardware and were flight-tested.

What still separates it from a production-quality PX4 board port is not the basic flight stack. The remaining work is mostly in five areas:

1. Production hygiene is incomplete in board startup and defaults.
   `board_adc` is never started, battery scaling parameters are missing, `safety_button` is never started, and `rc.board_defaults` still contains test-mode settings such as `SYS_HAS_GPS=0` despite a working GPS.
2. Platform completeness is below FMUv6X/FMUK66.
   The SAMV7 platform lacks `dshot`, `input_capture`, `tone_alarm`, `board_hw_info`, `led_pwm`, `pwm_trigger`, `spi` platform helpers, and a real `board_critmon`.
3. NuttX integration is incomplete even where SAMV71 hardware support already exists.
   MCAN, EMAC, TC capture, and PWMC lower-half support exist in NuttX SAMV7, but PX4 board/config glue is missing.
4. Fault handling is not production-safe.
   `sam_progmem.c` still executes flash write/erase paths from flash, while EEFC commands stall the flash bus. That blocks hardfault logging and crash dump support.
5. The SAMV71 board config is still demo-board oriented.
   No CAN/UAVCAN, no Ethernet, no tone alarm, no RC capture, no DShot, no watchdog in defconfig, and no persistent reset-state storage using GPBR.

The most important near-term fixes for the current XULT hardware are:
- start `board_adc` and configure battery scaling
- start `safety_button`
- fix `board_critmon` and enable watchdog
- fix NuttX progmem so hardfault logging works
- clean up production parameter defaults
- decide whether DShot, RC capture, CAN, and Ethernet are in-scope for this board before custom PCB work shifts attention elsewhere

## Files Reviewed

SAMV71 board and platform:
- `boards/microchip/samv71-xult-clickboards/default.px4board`
- `boards/microchip/samv71-xult-clickboards/src/*`
- `boards/microchip/samv71-xult-clickboards/init/*`
- `boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `platforms/nuttx/src/px4/microchip/samv7/**/*`

Reference board files emphasized:
- `boards/px4/fmu-v6x/default.px4board`
- `boards/px4/fmu-v6x/src/board_config.h`
- `boards/px4/fmu-v6x/src/init.cpp`
- `boards/px4/fmu-v6x/src/timer_config.cpp`
- `boards/px4/fmu-v6x/src/spi.cpp`
- `boards/px4/fmu-v6x/src/i2c.cpp`
- `boards/px4/fmu-v6x/init/rc.board_defaults`
- `boards/px4/fmu-v6x/init/rc.board_sensors`
- `boards/px4/fmu-v6x/nuttx-config/nsh/defconfig`
- `boards/nxp/fmuk66-v3/default.px4board`
- `boards/nxp/fmuk66-v3/src/board_config.h`
- `boards/nxp/fmuk66-v3/src/init.c`
- `boards/nxp/fmuk66-v3/src/timer_config.cpp`
- `boards/nxp/fmuk66-v3/init/rc.board_defaults`
- `boards/nxp/fmuk66-v3/init/rc.board_sensors`
- `boards/nxp/fmuk66-v3/nuttx-config/nsh/defconfig`
- `boards/px4/fmu-v6xrt/default.px4board`
- `boards/px4/fmu-v6xrt/src/board_config.h`
- `boards/px4/fmu-v6xrt/src/init.c`
- `boards/px4/fmu-v6xrt/src/timer_config.cpp`
- `boards/px4/fmu-v6xrt/init/rc.board_defaults`
- `boards/px4/fmu-v6xrt/init/rc.board_sensors`
- `boards/px4/fmu-v6xrt/nuttx-config/nsh/defconfig`

NuttX SAMV7 arch files checked in the sibling repo:
- `arch/arm/src/samv7/sam_progmem.c`
- `arch/arm/src/samv7/sam_eefc.c`
- `arch/arm/src/samv7/sam_mcan.c`
- `arch/arm/src/samv7/sam_emac.c`
- `arch/arm/src/samv7/sam_ethernet.c`
- `arch/arm/src/samv7/sam_tc.c`
- `arch/arm/src/samv7/sam_pwm.c`
- `boards/arm/samv7/samv71-xult/src/sam_mcan.c`

## 1. Feature-by-Feature Gap Table

| Feature / Module | FMUv6X / FMUK66 Reference | SAMV71 Status | What Is Missing On SAMV71 | Main Files | Effort |
| --- | --- | --- | --- | --- | --- |
| Multicopter flight stack | Full PX4 production baseline | Yes | None for basic MC flight | `default.px4board`, `init/rc.board_defaults` | - |
| Primary IMU on SPI | Multiple production IMUs and bus validation | Yes | No current gap for XULT hardware | `src/spi.cpp`, `init/rc.board_sensors` | - |
| Magnetometer | Internal/external mag handling, launchers | Yes | No current functional gap; only production parameter cleanup | `init/rc.board_sensors` | S |
| Barometer | Multi-baro support, internal/external selection | Yes | No current driver gap; baro health defaults need review | `init/rc.board_sensors`, `init/rc.board_defaults` | S |
| GPS serial | Reference boards expose richer serial routing | Yes | `SYS_HAS_GPS=0` is wrong for a board with working GPS | `init/rc.board_defaults` | S |
| SBUS RC via UART | FMUv6X also supports PWM input capture | Yes | Serial RC works; PWM input/capture path is still missing | `src/board_config.h`, `src/timer_config.cpp` | M |
| PWM motor output | Full IO timer stacks on refs | Yes | Only one PWMC timer block is surfaced; no capture or DShot mode | `src/timer_config.cpp`, `io_pins/io_timer_pwmc.c` | M |
| OneShot125 | Production refs use richer IO timer backends | Yes | None required for current XULT use | `io_pins/pwm_servo.c`, `io_pins/io_timer_pwmc.c` | - |
| Battery ADC and power reporting | Refs start `board_adc` and set battery params | Partial | `board_adc` is never started; no `BAT1_V_DIV` or `BAT1_A_PER_V`; MAVLink battery status stays invalid | `init/rc.board_sensors`, `init/rc.board_defaults`, `src/board_config.h` | S |
| Safety button + LED workflow | Production boards start `safety_button` | Partial | Hardware exists, driver not started, and test-mode defaults weaken safety posture | `src/board_config.h`, `init/rc.board_defaults` | S |
| CPU / RAM load reporting | Refs ship real `board_critmon` and watchdog | Partial | SAMV7 `board_critmon` is a stub; preflight reports no CPU/RAM load information | `platforms/.../board_critmon/board_critmon.c`, `nuttx-config/nsh/defconfig` | M |
| Hardfault logging / crashdump | FMUv6X enables crashdump and hardfault tools | No | `sam_progmem.c` flash write path is not SRAM-resident; hardfault logging disabled | NuttX `sam_progmem.c`, `sam_eefc.c`, `default.px4board`, `defconfig` | M-L |
| Watchdog | FMUv6X / FMUK66 / FMUv6XRT defconfigs enable it | No in defconfig | Need real NuttX/PX4 watchdog enable and validation on SAMV71 | `nuttx-config/nsh/defconfig`, `src/init.c` | S-M |
| DShot | FMUv6X and FMUv6XRT support it | No | No `dshot.c`; `io_timer_set_dshot_mode()` returns `-ENOSYS` | `platforms/.../dshot/`, `io_timer_pwmc.c`, `src/timer_config.cpp`, `default.px4board` | L |
| PWM input / RC capture | FMUv6X/FMUK66 have `input_capture.c` | No | No capture driver, no capture channel in timer config, `io_timer_tc.c` says PWM input not implemented | `io_pins/input_capture.c`, `io_timer_tc.c`, `src/timer_config.cpp`, `defconfig` | M |
| Tone alarm | FMUv6X/FMUK66 have tone alarm platform drivers | No | No SAMV7 tone alarm driver or board pin integration | `platforms/.../tone_alarm/`, `src/board_config.h`, `default.px4board` | M |
| CAN / UAVCAN | FMUv6X/FMUK66/FMUV6XRT support board-level CAN | No | NuttX MCAN exists, but SAMV71 PX4 board/config glue is absent | `src/can.c`, `src/init.c`, `defconfig`, `default.px4board` | M-L |
| Ethernet / network MAVLink | FMUv6X/FMUV6XRT enable Ethernet | No | NuttX EMAC exists, but no PX4 board/config integration | `src/init.c`, `src/board_config.h`, `defconfig`, `default.px4board` | M-L |
| USB VBUS detection | Production boards use real VBUS sense | Partial | `board_read_VBUS_state()` always reports VBUS present | `src/usb.c` | S-M |
| SD card logging | Production baseline feature | Yes | Production cleanup: resolve card-detect config mismatch | `src/sam_hsmci.c`, `defconfig` | S |
| QSPI params / caldata / waypoints | Comparable to production board flash partitions | Yes | Hardfault/crashdump storage still blocked by progmem issue | `src/qspi.c`, `src/init.c` | S |
| Persistent reset reason / boot mode | Production refs persist reset state | Partial | Reset mode only lives in RAM; TODO points to GPBR use | `platforms/.../board_reset/board_reset.cpp` | S-M |
| Board HW version / revision detection | FMUv6X / FMUv6XRT implement `board_hw_info` | No | No hardware revision/version support on SAMV71 | `platforms/.../board_hw_info/`, `src/init.c`, `src/board_config.h` | M |
| Unique board identity quality | Refs provide better MCU identity handling | Partial | SAMV7 board UUID path repeats CHIPID words instead of using a stronger unique source | `platforms/.../version/board_identity.c` | M |
| LED PWM / RGB LED | FMUK66 supports RGB LED PWM | No | Only simple GPIO LED exists; no PWM LED framework | `platforms/.../led_pwm/`, `default.px4board` | M |
| Camera trigger / capture | FMUv6X/FMUK66 support it | No | Driver and board-pin plumbing absent | `platforms/.../io_pins/pwm_trigger.c`, board GPIO config | M |
| PX4IO coprocessor | FMUv6X/FMUV6XRT support PX4IO | N/A | No PX4IO on XULT; only relevant for a future custom PCB that adds one | `px4io_serial`, `board_config.h` | - |
| Extra I2C buses | Refs expose more internal/external buses | Partial | Only TWIHS0 is surfaced; TWIHS1/TWIHS2 are unused in board and defconfig | `src/i2c.cpp`, `defconfig` | M |
| Extra UART / USART ports | Refs expose more telemetry and GPS ports | Partial | USART0, USART2, UART3 are not enabled; no extra serial mapping | `src/board_config.h`, `default.px4board`, `defconfig` | M |
| Extra ADC capability | Refs often expose more rail/USB/RSSI channels | Partial | AFEC1 is unused; no USB/RSSI/hardware-ID ADC channels | `src/board_config.h`, `defconfig` | M |
| Bootloader support | Production PX4 boards ship a bootloader path | No | Not integrated for SAMV71 | board bootloader integration, release tooling | M-L |
| Preflight parameter hygiene | Production boards keep checks enabled by default | Partial | Several test-era defaults weaken safety and health reporting | `init/rc.board_defaults` | S |

## 2. Platform Driver Gap Analysis

### 2.1 What Exists In `platforms/nuttx/src/px4/microchip/samv7`

Present:
- `adc/adc.cpp`
- `board_critmon/board_critmon.c`
- `board_reset/board_reset.cpp`
- `hrt/hrt.c`
- `io_pins/io_timer_pwmc.c`
- `io_pins/io_timer_tc.c`
- `io_pins/io_timer_stub.c`
- `io_pins/pwm_servo.c`
- `version/board_identity.c`
- `version/board_mcu_version.c`
- `include/px4_arch/{adc,dshot,hw_description,i2c_hw_description,io_timer,io_timer_hw_description,micro_hal,spi_hw_description}.h`

### 2.2 File-by-File Comparison Against STM32 Common and Kinetis

| Reference File / Module | STM32 / Kinetis / i.MX RT State | SAMV71 Equivalent | SAMV71 Status | Feasibility On SAMV71 | Notes |
| --- | --- | --- | --- | --- | --- |
| `adc/adc.cpp` | Present on all refs | `adc/adc.cpp` | Yes | High | Core board ADC support already exists; startup and params are the problem |
| `board_critmon/board_critmon.c` | Real implementation on STM32/i.MX RT | `board_critmon/board_critmon.c` | Partial | High | SAMV7 file is explicitly a stub; use Cortex-M7 DWT cycle counter like STM32/i.MX RT |
| `board_hw_info/board_hw_rev_ver.c` | Present on STM32/i.MX RT | None | No | High | Needed for future production PCB revisioning; not required for current XULT demo |
| `board_reset/board_reset.cpp` | Persistent reset mode on refs | `board_reset/board_reset.cpp` | Partial | High | SAMV7 TODO already points to GPBR backup registers |
| `dshot/dshot.c` | Present on STM32/i.MX RT | None | No | High | SAMV71 PWMC + XDMAC can support this; existing header already anticipates DShot |
| `hrt/hrt.c` | Present on all refs | `hrt/hrt.c` | Yes | High | Current HRT is already working on TC0 via PCK6 |
| `io_pins/input_capture.c` | Present on STM32/Kinetis/i.MX RT | None | No | High | Use TC5 capture path for RC/PWM input |
| `io_pins/io_timer.c` | Unified timer backends on refs | `io_timer_pwmc.c`, `io_timer_tc.c`, `io_timer_stub.c` | Partial | High | PWM output works, but capture and DShot paths are incomplete |
| `io_pins/pwm_servo.c` | Present on all refs | `pwm_servo.c` | Yes | High | Working for PWM and OneShot125 |
| `io_pins/pwm_trigger.c` | Present on STM32/Kinetis/i.MX RT | None | No | High | Feasible if camera trigger is desired; not essential on XULT |
| `io_pins/kinetis_pinirq.c` / `imxrt_pinirq.c` | GPIO interrupt helper | None | No | Medium | Needed if future input-capture/CAN/PHY IRQ plumbing wants a PX4-level helper |
| `led_pwm/led_pwm.cpp` | Present on STM32/Kinetis/i.MX RT | None | No | Medium | Feasible via PWMC if RGB or status brightness control is desired |
| `spi/spi.cpp` platform helper | Present on STM32/i.MX RT | None | No | High | Board-local SPI works now; platform helper becomes important once bus count or hardware variants grow |
| `srgbled_dma/srgbled_dma.cpp` | STM32-specific | None | No | Low | Possible only if you later need addressable LEDs; not needed now |
| `tone_alarm/ToneAlarmInterface*.cpp` | Present on STM32/Kinetis/i.MX RT | None | No | High | Straightforward with a spare PWM-capable pin or GPIO-driven buzzer |
| `version/board_identity.c` | Present on refs | `version/board_identity.c` | Partial | High | Current implementation is weaker than production references |
| `version/board_mcu_version.c` | Present on refs | `version/board_mcu_version.c` | Yes | High | Basic MCU version support exists |
| `px4io_serial/px4io_serial.cpp` | Present on STM32H7/RT117x | None | N/A | Medium | Only relevant if a future PCB adds PX4IO |
| `romapi/imxrt_romapi.c` | i.MX RT only | None | N/A | N/A | Not applicable to SAMV71 |
| `ssarc/ssarc_dump.c` | RT117x only | None | N/A | N/A | Not applicable to SAMV71 |

### 2.3 Specific SAMV71 Platform Gaps Observed In Source

Concrete gaps in current SAMV71 platform code:
- `platforms/nuttx/src/px4/microchip/samv7/board_critmon/board_critmon.c`
  - file comment explicitly says `stub implementation`
  - TODO says proper critical section timing is still missing
- `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c`
  - comments say DShot HW config is deferred to future `dshot.c`
  - `io_timer_set_dshot_mode()` returns `-ENOSYS`
- `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_tc.c`
  - PWM input path is not implemented
- `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h`
  - `px4_savepanic(...)` is a stub returning `0`
  - CAN filter notes say SAMV7 MCAN support is not fully integrated at PX4 arch level
- `platforms/nuttx/src/px4/microchip/samv7/board_reset/board_reset.cpp`
  - TODO points to GPBR registers `0x400E1890` to `0x400E18FC` for persistent reset state

## 3. NuttX Defconfig Comparison

### 3.1 What The Reference Defconfigs Enable That SAMV71 Does Not

| Area | FMUv6X / FMUK66 / FMUv6XRT | SAMV71 Current State | Action |
| --- | --- | --- | --- |
| Watchdog | `CONFIG_WATCHDOG=y` on all three references | Missing | Enable and validate reset/recovery behavior on SAMV71 |
| Crashdump / progmem visibility | `CONFIG_FS_PROCFS_INCLUDE_PROGMEM=y`, `CONFIG_STM32H7_SAVE_CRASHDUMP=y` on FMUv6X | Missing | Add equivalent support after fixing SAMV7 progmem safety |
| Ethernet | `CONFIG_NET=y`, PHY and EMAC configs on FMUv6X/FMUK66/FMUV6XRT | Missing | Enable `NET`, PHY IOCTLs, SAMV7 EMAC0 config, KSZ8061 PHY config |
| CAN networking | `CONFIG_NET_CAN=y` and CAN socket options on FMUv6XRT | Missing | Enable if using UAVCAN v1 / SocketCAN path |
| MCAN peripheral | FlexCAN/MCAN enabled on reference NXP boards | Missing | Enable `CONFIG_SAMV7_MCAN0` and/or `CONFIG_SAMV7_MCAN1` |
| Extra I2C buses | Multiple I2C controllers enabled on references | Only `CONFIG_SAMV7_TWIHS0=y` | Enable `TWIHS1` and `TWIHS2` if routed/needed |
| Extra UART/USART | Rich serial port mix on references | Only USART1, UART1, UART2, UART4 are enabled | Consider enabling USART0, USART2, UART3 |
| Extra timer channels | Multiple timers/capture channels on references | `TC0`, `TC1`, `TC3` enabled; no `TC5` | Enable `TC5` for RC capture, and `TC2/TC4` if needed |
| Additional ADC block | Refs expose more rails/monitor channels | No `AFEC1` | Enable if future PCB needs more analog inputs |
| Network management tools | Telnet / netman present on Ethernet-capable refs | Missing | Add if Ethernet support is brought up |

### 3.2 SAMV7-Specific Peripherals That Could Be Enabled

These are hardware-feasible on SAMV71 and are currently left unused by the PX4 board port:

| Peripheral | Hardware / NuttX Support | Current PX4 Board State | Recommendation |
| --- | --- | --- | --- |
| MCAN0 / MCAN1 | Supported in NuttX SAMV7 via `sam_mcan.c` | Not enabled in defconfig; no PX4 board glue | High-value medium-term addition |
| EMAC0 + KSZ8061 PHY | Supported in NuttX `sam_emac.c` / `sam_ethernet.c` | Not enabled in defconfig; no board net init | High-value medium-term addition |
| TC5 capture | Supported by `sam_tc_allocate()` and TC capture logic | Not enabled, no PX4 `input_capture.c` | High-value for PWM/PPM capture |
| TWIHS1 / TWIHS2 | Available on silicon | Not enabled, not surfaced in `src/i2c.cpp` | Medium |
| USART0 / USART2 / UART3 | Available on silicon | Not enabled | Medium |
| AFEC1 | Available on silicon | Not enabled | Medium |

### 3.3 Important Defconfig Findings

Specific findings from the current SAMV71 defconfig:
- Scheduler instrumentation is already enabled.
  The `load_mon` issue is therefore not a missing instrumentation config problem; it points to the SAMV7 `board_critmon` stub or related runtime plumbing.
- `CONFIG_SAMV7_PROGMEM=y` is already enabled.
  The hardfault logging issue is not a missing Kconfig switch; it is the unsafe flash-write execution path in `sam_progmem.c`.
- The current file still appears to contain a stray edit artifact on the `CONFIG_SAMV7_UART2=y` line.
  Clean this before upstreaming or long-term maintenance.
- SD card detect handling needs cleanup.
  The board notes say card detect is disabled because that pin is repurposed, but the defconfig still carries card-detect support. Current mount flow works because `src/init.c` manually waits and mounts, not because the card-detect configuration is coherent.

## 4. Init Script Comparison

### 4.1 `rc.board_defaults`

| Reference Behavior | SAMV71 Current Behavior | Gap |
| --- | --- | --- |
| FMUv6X and FMUv6XRT start `safety_button` in `rc.board_defaults` | SAMV71 never starts `safety_button` | Safety workflow is incomplete |
| FMUK66 sets battery calibration parameters in `rc.board_defaults` | SAMV71 sets no `BAT1_V_DIV` or `BAT1_A_PER_V` defaults | Battery telemetry remains unusable |
| Reference boards avoid contradictory GPS flags | SAMV71 sets `SYS_HAS_GPS=0` but later sets `GPS_1_CONFIG=201` | Production defaults are inconsistent |
| Reference boards keep board defaults small and board-focused | SAMV71 `rc.board_defaults` includes many test-era tuning and arming exceptions | Needs a production cleanup pass |

Notable SAMV71 defaults to review before calling the board production-ready:
- `SYS_HAS_GPS=0` with a working GPS on UART2
- `COM_CPU_MAX=-1`
- `COM_ARM_MAG_ANG=-1`
- `COM_ARM_HFLT_CHK=0`
- any remaining test-only circuit breakers or relaxed preflight thresholds

### 4.2 `rc.board_sensors`

| Reference Behavior | SAMV71 Current Behavior | Gap |
| --- | --- | --- |
| FMUv6X, FMUK66, and FMUv6XRT all start `board_adc` | SAMV71 never starts `board_adc` | Direct cause of missing battery telemetry |
| FMUv6X and FMUv6XRT start `i2c_launcher` on external buses | SAMV71 uses only direct hard-coded sensor startups | Less flexible sensor expansion and less parity with reference behavior |
| Reference boards optionally start INA power monitors | SAMV71 has no power monitor startup path | Acceptable on XULT, but not equivalent to production refs |
| References carry richer internal/external sensor selection logic | SAMV71 starts exactly `icm45686`, `bmm150`, `bmp388` | Fine for current hardware, but less production-general |

### 4.3 `rc.board_mavlink`

SAMV71 has a custom `rc.board_mavlink` that:
- starts `sercon`
- polls for `/dev/ttyACM0` for up to 5 seconds
- then starts USB MAVLink

That workaround is functional, but it currently depends on `board_read_VBUS_state()` always reporting VBUS present in `src/usb.c`. That is good enough for bench and flight testing, but not production-grade board detection.

### 4.4 `rc.board_extras`

SAMV71 manually starts `navigator` in `rc.board_extras`.

That is a board-local startup divergence that should be explained and either:
- kept intentionally with a comment stating why the common path is unsuitable, or
- removed once the root startup ordering issue is fixed

## 5. `default.px4board` Comparison

This section lists every `CONFIG_` option that appears on at least one reference board but not on SAMV71. The list is categorized by whether it is needed for the current XULT port, useful but optional, or not applicable to the current demo hardware.

### 5.1 Needed On Current XULT Port

| Option | Seen On | Why It Matters |
| --- | --- | --- |
| `CONFIG_BOARD_ETHERNET` | FMUv6X, FMUv6XRT | The XULT board has on-board Ethernet hardware; this is a real software gap |
| `CONFIG_COMMON_TELEMETRY` | FMUK66, FMUv6XRT | Worth enabling once extra serial/network telemetry paths are exposed |
| `CONFIG_DRIVERS_DSHOT` | FMUv6X, FMUv6XRT | Missing motor protocol capability relative to production PX4 FCs |
| `CONFIG_DRIVERS_PWM_INPUT` | FMUv6X | Needed if RC capture/PWM input on TC5 is implemented |
| `CONFIG_DRIVERS_TONE_ALARM` | FMUv6X, FMUK66, FMUv6XRT | Standard PX4 board feature missing on SAMV71 |
| `CONFIG_DRIVERS_UAVCAN` | FMUv6X, FMUK66, FMUv6XRT | MCAN-capable silicon is present but unused |
| `CONFIG_MODULES_GYRO_FFT` | FMUK66, FMUv6XRT | Useful for the vibration issues seen in flight logs |
| `CONFIG_MODULES_HARDFAULT_STREAM` | FMUv6X, FMUv6XRT | Needed once crashdump/hardfault capture is fixed |
| `CONFIG_SYSTEMCMDS_HARDFAULT_LOG` | FMUv6X, FMUv6XRT | Needed once progmem-safe logging exists |
| `CONFIG_SYSTEMCMDS_I2C_LAUNCHER` | FMUv6X, FMUv6XRT | Brings sensor startup closer to production PX4 boards |
| `CONFIG_SYSTEMCMDS_NETMAN` | FMUv6X, FMUv6XRT | Needed if Ethernet is enabled |
| `CONFIG_SYSTEMCMDS_USB_CONNECTED` | FMUK66, FMUv6XRT | Useful if USB connection state is made real instead of stubbed |

### 5.2 Nice-To-Have / Future Custom PCB Or Expanded XULT Testing

| Option | Seen On | Why It Is Optional Rather Than Required |
| --- | --- | --- |
| `CONFIG_BOARD_SERIAL_EXT2` | FMUv6X | Useful only if more external serial routing is exposed |
| `CONFIG_BOARD_SERIAL_GPS2` | FMUv6X, FMUv6XRT | Useful on a richer carrier/custom PCB |
| `CONFIG_BOARD_SERIAL_TEL3` | FMUv6X, FMUv6XRT | Same |
| `CONFIG_BOARD_UAVCAN_TIMER_OVERRIDE` | FMUv6X | Useful only once CAN and board timer interactions matter |
| `CONFIG_COMMON_BAROMETERS` | FMUK66 | Framework convenience, not a blocker |
| `CONFIG_COMMON_DIFFERENTIAL_PRESSURE` | FMUv6X, FMUK66, FMUv6XRT | Future fixed-wing use, not current XULT multicopter use |
| `CONFIG_COMMON_DISTANCE_SENSOR` | FMUv6X, FMUK66, FMUv6XRT | Optional payload/peripheral support |
| `CONFIG_COMMON_INS` | FMUv6XRT | Not needed for current sensor set |
| `CONFIG_COMMON_LIGHT` | FMUv6X, FMUv6XRT | Optional lighting subsystem |
| `CONFIG_COMMON_MAGNETOMETER` | FMUv6X, FMUK66, FMUv6XRT | Current specific mag driver already works |
| `CONFIG_COMMON_RC` | FMUv6X, FMUv6XRT | Current serial SBUS path already works without it |
| `CONFIG_DRIVERS_CAMERA_CAPTURE` | FMUv6X, FMUK66, FMUv6XRT | Useful on a richer airframe, not current XULT need |
| `CONFIG_DRIVERS_CAMERA_TRIGGER` | FMUv6X, FMUK66, FMUv6XRT | Same |
| `CONFIG_DRIVERS_HEATER` | FMUv6X | Only if a future PCB adds sensor heating |
| `CONFIG_DRIVERS_LIGHTS_RGBLED` | FMUK66 | Cosmetic / UX improvement |
| `CONFIG_DRIVERS_LIGHTS_RGBLED_NCP5623C` | FMUK66 | Needs matching hardware |
| `CONFIG_DRIVERS_LIGHTS_RGBLED_PWM` | FMUK66 | Feasible if a PWM RGB LED is added |
| `CONFIG_DRIVERS_PCA9685_PWM_OUT` | FMUK66 | External PWM expander support, not current board need |
| `CONFIG_DRIVERS_POWER_MONITOR_INA226` | All refs except SAMV71 | Useful if future board adds INA rails or power monitors |
| `CONFIG_DRIVERS_POWER_MONITOR_INA228` | FMUv6X, FMUv6XRT | Same |
| `CONFIG_DRIVERS_POWER_MONITOR_INA238` | FMUv6X, FMUv6XRT | Same |
| `CONFIG_DRIVERS_UWB_UWB_SR150` | FMUK66 | Optional feature |
| `CONFIG_MAVLINK_DIALECT` | FMUv6X | Useful if custom dialect selection is needed |
| `CONFIG_MODULES_AIRSPEED_SELECTOR` | All refs | Needed only for fixed-wing / differential pressure workflows |
| `CONFIG_MODULES_ATTITUDE_ESTIMATOR_Q` | FMUK66 | Legacy/alternate estimator, not needed with EKF2 |
| `CONFIG_MODULES_CAMERA_FEEDBACK` | All refs | Optional payload support |
| `CONFIG_MODULES_ESC_BATTERY` | All refs | Useful once smart ESC telemetry exists |
| `CONFIG_MODULES_GIMBAL` | All refs | Optional payload support |
| `CONFIG_MODULES_LANDING_TARGET_ESTIMATOR` | All refs | Optional precision landing feature |
| `CONFIG_MODULES_LOCAL_POSITION_ESTIMATOR` | FMUK66, FMUv6XRT | Alternate estimator, not required |
| `CONFIG_MODULES_SIMULATION_SIMULATOR_SIH` | FMUK66 | Nice for simulation coverage |
| `CONFIG_MODULES_TEMPERATURE_COMPENSATION` | All refs | Good production feature, but not a blocker for current XULT |
| `CONFIG_MODULES_UXRCE_DDS_CLIENT` | All refs | Optional middleware feature |
| `CONFIG_SYSTEMCMDS_DUMPFILE` | FMUK66, FMUv6XRT | Useful for debugging, not a blocker |
| `CONFIG_SYSTEMCMDS_GPIO` | FMUv6X | Helpful for bench debug |
| `CONFIG_SYSTEMCMDS_REFLECT` | FMUK66 | Debug utility only |
| `CONFIG_SYSTEMCMDS_SD_BENCH` | FMUK66, FMUv6XRT | Debug/validation utility |
| `CONFIG_SYSTEMCMDS_SD_STRESS` | FMUK66, FMUv6XRT | Debug/validation utility |
| `CONFIG_SYSTEMCMDS_SERIAL_TEST` | FMUK66, FMUv6XRT | Debug/validation utility |

### 5.3 Not Applicable To The Current XULT Demo Hardware

| Option | Seen On | Why It Is Not Applicable |
| --- | --- | --- |
| `CONFIG_BOARD_CONSTRAINED_MEMORY` | FMUK66 | FMUK66-specific memory tradeoff |
| `CONFIG_COMMON_OPTICAL_FLOW` | FMUK66 | No optical-flow hardware on the XULT setup |
| `CONFIG_COMMON_UWB` | FMUv6XRT | No UWB hardware on XULT |
| `CONFIG_DRIVERS_ADC_ADS1115` | All refs | No ADS1115 on the current board |
| `CONFIG_DRIVERS_BAROMETER_INVENSENSE_ICP201XX` | FMUv6X, FMUv6XRT | Different barometer hardware |
| `CONFIG_DRIVERS_BAROMETER_MPL3115A2` | FMUK66 | Different barometer hardware |
| `CONFIG_DRIVERS_BAROMETER_MS5611` | FMUv6X, FMUv6XRT | Different barometer hardware |
| `CONFIG_DRIVERS_BATT_SMBUS` | FMUK66 | No SMBus smart battery on current XULT setup |
| `CONFIG_DRIVERS_DIFFERENTIAL_PRESSURE_AUAV` | FMUv6X, FMUv6XRT | No differential pressure sensor on current setup |
| `CONFIG_DRIVERS_DISTANCE_SENSOR_LIGHTWARE_SF45_SERIAL` | FMUv6XRT | No such sensor on current setup |
| `CONFIG_DRIVERS_DISTANCE_SENSOR_SRF05` | FMUK66 | No such sensor on current setup |
| `CONFIG_DRIVERS_GNSS_SEPTENTRIO` | FMUv6X, FMUv6XRT | Not relevant to current GPS module |
| `CONFIG_DRIVERS_IMU_ANALOG_DEVICES_ADIS16470` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_ICM20602` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_ICM20649` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_ICM20948` | FMUv6X, FMUK66, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_ICM42670P` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_ICM42688P` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_INVENSENSE_IIM42652` | FMUv6X, FMUv6XRT | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_NXP_FXAS21002C` | FMUK66 | Not current IMU hardware |
| `CONFIG_DRIVERS_IMU_NXP_FXOS8701CQ` | FMUK66 | Not current IMU hardware |
| `CONFIG_DRIVERS_IRLOCK` | FMUK66 | No IRLOCK hardware on current setup |
| `CONFIG_DRIVERS_MAGNETOMETER_BOSCH_BMM350` | FMUv6XRT | Not current magnetometer hardware |
| `CONFIG_DRIVERS_MAGNETOMETER_LIS2MDL` | FMUv6XRT | Not current magnetometer hardware |
| `CONFIG_DRIVERS_OSD_MSP_OSD` | FMUv6X, FMUv6XRT | No OSD hardware path on current setup |
| `CONFIG_DRIVERS_PX4IO` | FMUv6X, FMUv6XRT | No PX4IO co-processor on XULT |
| `CONFIG_DRIVERS_SMART_BATTERY_BATMON` | FMUK66 | No BatMon hardware |
| `CONFIG_EXAMPLES_FAKE_GPS` | FMUK66 | Example only, not a board requirement |
| `CONFIG_MODE_NAVIGATOR_VTOL_TAKEOFF` | FMUv6X, FMUv6XRT | Current use case is multicopter only |
| `CONFIG_MODULES_FW_ATT_CONTROL` | All refs | Fixed-wing only |
| `CONFIG_MODULES_FW_AUTOTUNE_ATTITUDE_CONTROL` | All refs | Fixed-wing only |
| `CONFIG_MODULES_FW_LATERAL_LONGITUDINAL_CONTROL` | All refs | Fixed-wing only |
| `CONFIG_MODULES_FW_MODE_MANAGER` | All refs | Fixed-wing only |
| `CONFIG_MODULES_FW_RATE_CONTROL` | All refs | Fixed-wing only |
| `CONFIG_MODULES_VTOL_ATT_CONTROL` | All refs | VTOL only |
| `CONFIG_MODULES_ZENOH` | FMUv6XRT | Optional middleware, not board-specific |
| `CONFIG_NUM_MISSION_ITMES_SUPPORTED` | FMUv6X, FMUv6XRT | Not a board-completeness issue for current XULT work |
| `CONFIG_SYSTEMCMDS_IO_BYPASS_CONTROL` | FMUv6XRT | PX4IO-centric functionality |

## 6. Prioritized Implementation Roadmap

### CRITICAL

1. Battery telemetry and production startup cleanup
   Start `board_adc`, add battery scaling defaults, start `safety_button`, fix contradictory GPS defaults, and remove test-only arming exceptions.
2. CPU/RAM load reporting and watchdog completion
   Replace the SAMV7 `board_critmon` stub with a real implementation and enable the watchdog in defconfig.
3. Hardfault logging / crashdump support
   Fix the NuttX SAMV7 progmem path so flash erase/write operations used by crash logging execute safely from SRAM, then enable PX4 hardfault tools.

### HIGH

1. DShot support on PWMC + XDMAC
2. RC capture / PWM input on TC5
3. CAN / UAVCAN integration using SAMV7 MCAN
4. Ethernet integration using on-board KSZ8061 PHY
5. Real USB VBUS sensing and serial robustness cleanup
6. Persistent reset-state storage and stronger board identity handling

### MEDIUM

1. Tone alarm
2. Bootloader integration
3. Additional I2C/UART/AFEC1 exposure
4. `i2c_launcher` parity with production boards
5. `CONFIG_MODULES_TEMPERATURE_COMPENSATION`
6. LED PWM / richer status indication
7. Camera trigger/capture support if needed on the custom PCB

### LOW

1. Fixed-wing and VTOL module parity
2. Optional peripheral drivers not present on XULT hardware
3. UXRCE-DDS / Zenoh / UWB / OSD / IRLOCK / fake GPS parity

## 7. Detailed Work Items For CRITICAL And HIGH Priorities

### 7.1 CRITICAL: Battery Telemetry And Production Startup Cleanup

Files to modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/init/rc.board_defaults`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/init/rc.board_sensors`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/board_config.h`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/init/rc.board_defaults`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/init/rc.board_sensors`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/nxp/fmuk66-v3/init/rc.board_defaults`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/nxp/fmuk66-v3/init/rc.board_sensors`

What the implementation involves:
- start `board_adc` before battery-dependent modules
- set correct `BAT1_V_DIV`, `BAT1_A_PER_V`, and related battery source parameters
- start `safety_button`
- set `SYS_HAS_GPS=1` if GPS is a standard population on this board profile
- remove or justify test-era defaults that disable production preflight checks

SAMV7 hardware considerations:
- battery voltage/current already map to AFEC0 CH0 and CH7
- `BOARD_ADC_BRICK_VALID` is hard-coded valid, so software scaling is the missing piece
- safety button is on `PA9` and safety LED is on `PC9`

Complexity:
- `S`

Dependencies:
- none

### 7.2 CRITICAL: CPU/RAM Load Reporting And Watchdog Completion

Files to modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/board_critmon/board_critmon.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/init.c`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/board_critmon/board_critmon.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/nxp/imxrt/board_critmon/board_critmon.c`

What the implementation involves:
- implement `up_critmon_gettime()` using Cortex-M7 DWT cycle counting and CPU clock scaling
- validate that `load_mon` now reports CPU/RAM load and clears the current preflight warning
- enable `CONFIG_WATCHDOG=y` and verify reset/recovery sequencing

SAMV7 hardware considerations:
- Cortex-M7 DWT cycle counter is available on SAMV71 and is the cleanest match to STM32/i.MX RT reference behavior
- watchdog reset behavior must coexist with QSPI parameter storage and SD logging

Complexity:
- `M`

Dependencies:
- none

### 7.3 CRITICAL: Hardfault Logging / Crashdump Support

Files to modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_progmem.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_eefc.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/default.px4board`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/include/px4_arch/micro_hal.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/default.px4board`

What the implementation involves:
- move the full flash command path used by `up_progmem_write()` and `up_progmem_eraseblock()` into SRAM-safe execution, not just the low-level EEFC helpers
- validate that hardfault logging no longer hangs while issuing EEFC commands
- then enable `CONFIG_SYSTEMCMDS_HARDFAULT_LOG`, `CONFIG_MODULES_HARDFAULT_STREAM`, and supporting NuttX crash/progmem options
- consider using GPBR for minimal panic breadcrumbs because SAMV71 lacks STM32-style battery-backed SRAM

SAMV7 hardware considerations:
- EEFC commands stall flash access, so any code fetching instructions from flash during write/erase can deadlock
- `sam_eefc.c` already uses `__ramfunc__`; `sam_progmem.c` currently does not
- GPBR registers exist at `0x400E1890` to `0x400E18FC`

Complexity:
- `M-L`

Dependencies:
- none

### 7.4 HIGH: DShot Support On PWMC + XDMAC

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/dshot/dshot.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/timer_config.cpp`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/default.px4board`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/dshot/dshot.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/nxp/imxrt/dshot/dshot.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6xrt/src/timer_config.cpp`

What the implementation involves:
- add a SAMV7 `dshot.c` backend
- teach `timer_config.cpp` which output channels are DShot-capable
- use PWMC synchronized update channels and XDMAC to stream pulse patterns
- complete `io_timer_set_dshot_mode()` and any DMA request/update plumbing left stubbed in `io_timer_pwmc.c`

SAMV7 hardware considerations:
- current outputs are PWMC0 channels on `PB0`, `PA2`, `PC19`, `PC13`
- base clock is `MCK = 150 MHz`
- implementation likely wants PWMC synchronous update (`PWM_SCM`, channel update registers) plus XDMAC handshakes
- disarmed/safety behavior must still force outputs inactive during reset and arming transitions

Complexity:
- `L`

Dependencies:
- none

### 7.5 HIGH: RC Capture / PWM Input On TC5

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/io_pins/input_capture.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_tc.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/timer_config.cpp`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/board_config.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/default.px4board`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/nxp/kinetis/io_pins/input_capture.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/io_pins/input_capture.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/timer_config.cpp`

What the implementation involves:
- add a SAMV7 `input_capture.c`
- enable the TC channel used for capture in defconfig
- define the capture channel in `timer_config.cpp`
- wire the PX4 PWM-input driver to the TC capture backend

SAMV7 hardware considerations:
- current board code reserves `GPIO_RC_INPUT` on `PC29` for `TC5 TIOA`
- the user notes also mention a `PD18` conflict around SD card detect versus RC capture
- before implementation, re-verify the actual routed capture pin on the XULT board because the current notes are inconsistent
- NuttX `sam_tc.c` already supports `sam_tc_allocate()` and PCK6-backed timing

Complexity:
- `M`

Dependencies:
- none

### 7.6 HIGH: CAN / UAVCAN Integration

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/can.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/init.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/board_config.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/default.px4board`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/micro_hal.h`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/can.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6xrt/src/can.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/nxp/fmuk66-v3/src/init.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_mcan.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/boards/arm/samv7/samv71-xult/src/sam_mcan.c`

What the implementation involves:
- enable MCAN peripheral(s) in defconfig
- add board-level CAN bring-up and pin definitions
- enable PX4 `uavcan` in `default.px4board`
- verify socket/CAN path requirements if using UAVCAN v1 transport

SAMV7 hardware considerations:
- MCAN is present on SAMV71 and NuttX support already exists
- user-provided available pins are `PC14` and `PC12`
- need a transceiver and any standby/enable pins correctly defined for the actual board path under test
- verify interrupt routing and message RAM allocation choices in `sam_mcan.c`

Complexity:
- `M-L`

Dependencies:
- if using SocketCAN or modern CAN stack plumbing, also enable relevant network/CAN defconfig options

### 7.7 HIGH: Ethernet Integration

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/init.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/board_config.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/default.px4board`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/init.cpp`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/board_config.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6xrt/src/init.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_ethernet.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Private/platforms/nuttx/NuttX/nuttx/arch/arm/src/samv7/sam_emac.c`

What the implementation involves:
- enable NuttX `NET` + EMAC + PHY config
- bring up Ethernet in board init, analogous to how FMUv6X/FMUV6XRT call net initialization
- add PX4 `CONFIG_BOARD_ETHERNET` and optionally `netman`

SAMV7 hardware considerations:
- the XULT board includes a KSZ8061 PHY
- NuttX SAMV7 EMAC already contains KSZ8061 handling
- need correct PHY address, RMII/MII selection, and link-status configuration
- verify whether the board uses PHY polling or a dedicated link interrupt

Complexity:
- `M-L`

Dependencies:
- depends on defconfig network stack enablement

### 7.8 HIGH: Real USB VBUS Sensing And Serial Robustness

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/usb.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/board_config.h`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/init/rc.board_mavlink`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/init.c`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/usb.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6xrt/src/usb.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/px4/fmu-v6x/src/init.cpp`

What the implementation involves:
- replace the current always-true VBUS stub with actual board sensing
- remove any USB startup logic that depends on fake VBUS state
- investigate whether GCS dropouts are worsened by the lack of serial DMA-style polling/workarounds used on STM32 and i.MX RT boards

SAMV7 hardware considerations:
- current `board_read_VBUS_state()` simply returns `0` so `cdcacm_autostart` always sees VBUS present
- if available, use the board's VBUS sense GPIO or a USBHS status path
- for telemetry robustness, inspect whether SAMV7 UART/USART DMA or PDC/XDMAC support can be used for high-rate links

Complexity:
- `M`

Dependencies:
- none

### 7.9 HIGH: Persistent Reset State And Stronger Board Identity

Files to create or modify:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/board_reset/board_reset.cpp`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/version/board_identity.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/boards/microchip/samv71-xult-clickboards/src/init.c`
- optional for future PCB revisioning: create `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/microchip/samv7/board_hw_info/board_hw_rev_ver.c`

Reference implementations:
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/board_reset/board_reset.cpp`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/board_hw_info/board_hw_rev_ver.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/nxp/imxrt/board_hw_info/board_hw_rev_ver.c`
- `/media/bhanu1234/Development/PX4-Autopilot-Rathi/platforms/nuttx/src/px4/stm/stm32_common/version/board_identity.c`

What the implementation involves:
- store reset mode in GPBR instead of RAM-only state
- strengthen board UUID generation if SAMV71 exposes a better unique source than the current repeated CHIPID-derived value
- add board revision/version detection only if the future custom PCB implements hardware-ID straps or resistor ladders

SAMV7 hardware considerations:
- GPBR backup registers are available and already referenced in the SAMV7 TODO
- XULT demo hardware may not have ADC strap ladders for `board_hw_info`, so board revisioning is mostly a future-PCB requirement

Complexity:
- `M`

Dependencies:
- none

## 8. Additional Notes Tied To Flight-Test Observations

These are not all pure board-port bugs, but they should influence what gets prioritized:

- High vibration on Z with IMU clipping
  - enabling `CONFIG_MODULES_GYRO_FFT` is worthwhile
  - also review mount isolation and IMU notch/filter defaults separately
- Barometer altitude drift
  - mostly mechanical/shielding, but board defaults should not disable useful preflight health checks
- No battery/ESC telemetry in MAVLink
  - directly explained by missing `board_adc` startup and missing battery scaling defaults
- `Preflight Fail: heading estimate not stable`
  - not primarily a SAMV7 platform gap; address through mag calibration, orientation sanity, and EKF2 defaults
- `Preflight Fail: No CPU and RAM load information`
  - consistent with the current SAMV7 `board_critmon` stub and missing watchdog/config cleanup
- GCS link dropout during one flight
  - check UART/USB buffering and whether a serial DMA/PDC strategy is needed, especially if telemetry rates increase

## 9. Bottom Line

For the current SAMV71-XULT hardware, the remaining work is finite and mostly practical:

- three critical fixes are needed before calling the port production-quality on the current board:
  - battery/safety/startup cleanup
  - load monitoring plus watchdog
  - hardfault logging support
- four major capability gaps remain compared with mature PX4 FC ports:
  - DShot
  - RC capture
  - CAN/UAVCAN
  - Ethernet
- the underlying silicon and NuttX support are already there for MCAN, EMAC, TC capture, and PWMC
  - this is mostly missing PX4 board/platform integration, not a fundamental SAMV71 limitation

If the goal is "production-quality multicopter port on the current XULT hardware" rather than "feature parity with every FMUv6X option," the shortest credible path is:

1. fix startup/defaults, battery telemetry, safety button, load monitoring, watchdog, and hardfault logging
2. then choose between DShot and CAN/Ethernet as the next platform-completeness milestone
3. leave fixed-wing, VTOL, PX4IO, exotic sensor drivers, and middleware parity for the custom PCB phase
