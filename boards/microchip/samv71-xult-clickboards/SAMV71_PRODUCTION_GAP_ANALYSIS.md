# SAMV71 Production Gap Analysis — Software Features Remaining

**Date:** 2026-04-02
**Reference codebase:** PX4-Autopilot-Rathi `samv7-custom` branch (commit `be2a93b270`)
**Compared against:** STM32 FMUv6X, NXP FMUK66-V3, NXP i.MX RT

---

## Current Feature Status (What Works — Flight Tested 2026-03-25)

### Fully Working & Flight Proven
| Subsystem | Implementation | Hardware |
|-----------|---------------|----------|
| IMU | ICM-45686 on SPI0 (PD27 CS) | Click board |
| Magnetometer | BMM150 on I2C (TWIHS0) | Click board |
| Barometer | BMP388 on I2C (TWIHS0) | Click board |
| PWM Output | 4-ch PWMC0 (400Hz, dynamic prescaler) | PB0/PA2/PC19/PC13 |
| OneShot125 | PWMC-based | Same pins |
| GPS | UART2 /dev/ttyS2 @ 38400 | External module |
| RC Input | SBUS via UART4 /dev/ttyS3 @ 100000/8E2 | RadioMaster TX16S + R81 |
| USB MAVLink | CDC/ACM /dev/ttyACM0 | J500 connector |
| Console | USART1 /dev/ttyS0 @ 115200 | EDBG VCOM |
| SD Card | HSMCI0 FAT32, /fs/microsd | On-board slot |
| QSPI Flash | S25FL116K 2MB — params/caldata/waypoints MTD partitions | On-board |
| Safety Button | PA9 active-low with LED on PC9 | SW0 button |
| Battery ADC | AFEC0 CH0 (voltage) + CH7 (current) | Analog input |
| HRT | TC0 via PCK6 @ 1MHz | Internal |
| Watchdog | Enabled | Internal |
| HITL | jMAVSim verified (takeoff, flight, landing) | USB |
| EKF2 | Full MC stack (att/rate/pos control) | — |
| Logging | ULog to SD card (armed mode) | — |
| TRNG | True RNG via SAMV7 TRNG peripheral | Internal |

### Compiled But Not Active (drivers present, not started)
- ICM20689, BMI088 (SPI/I2C), AK09916, DPS310

### Known Disabled
- HAS_PROGMEM / Hardfault logging (NuttX sam_progmem.c hang)
- DShot (plan exists, no code)
- Tone Alarm (no driver)
- CAN/UAVCAN (pins defined, no driver integration)
- Bootloader (not built)
- RC Capture via TC5 (GPIO reserved, not configured)

---

## Gap Analysis — Software Items Implementable on Demo Board

### TIER 1 — CRITICAL (Must-have for production-grade PX4)

#### 1.1 DShot ESC Protocol
- **What:** Digital ESC protocol via PWMC Sync Channel Mode + XDMAC DMA
- **Why:** ESC telemetry (RPM, temp, voltage), faster response, no PWM calibration needed
- **STM32 ref:** `platforms/nuttx/src/px4/stm/stm32_common/dshot/dshot.c` (DMA burst to timer DMAR)
- **SAMV7 approach:** PWMC SCM (UPDM=2) + XDMAC to DMAR register, CPRE=1 (MCK/2=75MHz)
- **Plan:** `DSHOT_PHASE2_BUILD_PLAN.md` (deleted from Rathi's tree, exists in mother repo)
- **Files to create:**
  - `platforms/nuttx/src/px4/microchip/samv7/dshot/dshot.c`
  - `platforms/nuttx/src/px4/microchip/samv7/dshot/CMakeLists.txt`
- **Files to modify:**
  - `platforms/nuttx/src/px4/microchip/samv7/io_pins/io_timer_pwmc.c` (add sync mode helpers)
  - `platforms/nuttx/src/px4/microchip/samv7/include/px4_arch/io_timer.h` (declare helpers)
  - `platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt` (add dshot subdir)
  - `boards/microchip/samv71-xult-clickboards/default.px4board` (enable CONFIG_DRIVERS_DSHOT)
- **Effort:** Large — 9-step implementation, needs oscilloscope validation
- **Demo board testable:** Yes (same 4 motor pins)

#### 1.2 Tone Alarm / Buzzer Driver
- **What:** Audible alerts for pre-arm failures, low battery, GPS loss, arming, etc.
- **Why:** Required safety feature — pilot has no audio feedback without it
- **STM32 ref:** `platforms/nuttx/src/px4/stm/stm32_common/tone_alarm/ToneAlarmInterfacePWM.cpp` (timer PWM)
- **NXP ref:** `platforms/nuttx/src/px4/nxp/kinetis/tone_alarm/ToneAlarmInterface.cpp` (TPM peripheral)
- **SAMV7 approach:** Use a spare TC channel (TC1 CH1 = TC4, or TC2 CH0 = TC6) in waveform mode to generate square wave on a GPIO pin. Alternatively use PWMC CH spare if available.
- **Files to create:**
  - `platforms/nuttx/src/px4/microchip/samv7/tone_alarm/ToneAlarmInterface.cpp`
  - `platforms/nuttx/src/px4/microchip/samv7/tone_alarm/CMakeLists.txt`
- **Files to modify:**
  - `platforms/nuttx/src/px4/microchip/samv7/CMakeLists.txt`
  - `boards/microchip/samv71-xult-clickboards/src/board_config.h` (define TONE_ALARM pin)
  - `boards/microchip/samv71-xult-clickboards/default.px4board` (enable driver)
- **Effort:** Medium
- **Demo board testable:** Yes (needs a piezo buzzer on a GPIO)

#### 1.3 Hardfault Logging / Crash Persistence
- **What:** Save crash dump to non-volatile storage on hardfault
- **Why:** Without this, crashes are undiagnosable — critical for field debugging
- **STM32 approach:** BBSRAM (battery-backed SRAM) — 5 files saved across reset
- **SAMV7 blocker:** `up_progmem_write()` in NuttX `sam_progmem.c` is not `__ramfunc__`, hangs when EEFC commands execute from flash
- **SAMV7 fix needed:** Patch NuttX `sam_progmem.c` — mark `up_progmem_write()` and `up_progmem_erasepage()` as `__ramfunc__` (same as `sam_eefc.c` functions)
- **Alternative:** Write crash dump to QSPI flash (already working) or SD card (if mounted)
- **Files to modify:**
  - NuttX: `arch/arm/src/samv7/sam_progmem.c` (add `__ramfunc__`)
  - PX4: Re-enable `HAS_PROGMEM` in board_config.h
  - PX4: Enable `CONFIG_SYSTEMCMDS_HARDFAULT_LOG` in px4board
- **Effort:** Medium (NuttX patch) or Small (QSPI/SD alternative)
- **Demo board testable:** Yes

#### 1.4 Bootloader
- **What:** PX4 bootloader for field firmware updates via USB/UART
- **Why:** Cannot update firmware without JTAG/SWD without a bootloader
- **STM32 ref:** `boards/px4/fmuv6x/bootloader.px4board` + linker scripts
- **NXP ref:** VBAT register magic value (`0xb007b007`) for bootloader entry from app
- **SAMV7 approach:** Use GPBR (General Purpose Backup Register) for bootloader entry magic, EEFC flash write for firmware update, USB CDC or UART for transfer
- **Files to create:**
  - `boards/microchip/samv71-xult-clickboards/bootloader.px4board`
  - `boards/microchip/samv71-xult-clickboards/nuttx-config/bootloader/defconfig`
  - Bootloader linker script
  - `board_reset_enter_bootloader()` implementation
- **Effort:** Large
- **Demo board testable:** Yes

---

### TIER 2 — HIGH (Required for reliable autonomous flight)

#### 2.1 CAN Bus / UAVCAN Integration
- **What:** Enable MCAN peripheral for UAVCAN/Cyphal device bus
- **Why:** Production drones use CAN-bus GPS, airspeed sensors, power monitors, ESCs
- **Hardware:** SAMV71 has MCAN0 (PB3/PB2) and MCAN1 (PC14/PC12) — pins defined in board_config.h
- **NuttX:** `CONFIG_SAMV7_MCAN0` / `CONFIG_SAMV7_MCAN1` available
- **Files to modify:**
  - `nuttx-config/nsh/defconfig` (enable MCAN, SocketCAN)
  - `default.px4board` (enable UAVCAN driver)
  - `board_config.h` (CAN transceiver enable GPIOs if needed)
- **Effort:** Medium
- **Demo board testable:** Yes (MCAN1 pins on headers, needs CAN transceiver click board)

#### 2.2 RC Input Capture via Timer (TC5)
- **What:** Hardware timer capture for PPM/PWM RC input instead of UART-only SBUS
- **Why:** Supports PPM receivers, more accurate timing, frees UART for other use
- **Pin:** PC29 (TIOA5) already reserved as `GPIO_RC_INPUT`
- **Conflict:** PD18 shared with SD card detect (already disabled)
- **STM32 ref:** `io_timer.c` input capture mode
- **NXP ref:** `input_capture.c` for FTM-based capture
- **Files to modify:**
  - `timer_config.cpp` (add TC5 as input capture timer)
  - `io_timer_pwmc.c` or new TC capture driver
  - `defconfig` (enable TC1 CH2 = TC5)
- **Effort:** Medium-Large
- **Demo board testable:** Yes

#### 2.3 Board Peripheral Reset
- **What:** `board_peripheral_reset()` — power-cycle sensors on fault recovery
- **Why:** STM32 FMUv6X uses this to recover from sensor lockups without full reboot
- **SAMV7 approach:** Toggle sensor power GPIO (if available), or SPI bus reset sequence
- **NXP ref:** `board_spi_reset()` in FMUK66 — full power-cycle + 10ms delay + re-init
- **Files to modify:**
  - `board_config.h` (define sensor power GPIO if available)
  - `init.c` or new `board_reset.c` (implement reset sequence)
- **Effort:** Small-Medium
- **Demo board testable:** Yes (if click board power is controllable)

#### 2.4 UART DMA
- **What:** Enable DMA for UART RX/TX to reduce CPU overhead on serial I/O
- **Why:** GPS and telemetry at high rates consume CPU without DMA
- **NuttX:** SAMV7 UART DMA support exists (`CONFIG_SAMV7_UART2_DMA` etc.)
- **Files to modify:**
  - `defconfig` (enable UART DMA for GPS/RC/TEL ports)
- **Effort:** Small
- **Demo board testable:** Yes

#### 2.5 SPI DMA Optimization
- **What:** Ensure SPI DMA is fully utilized for IMU reads
- **Current:** `CONFIG_SAMV7_SPI_DMA=y` with 4-byte threshold
- **Check:** Verify IMU reads actually use DMA path (may need NuttX SPI driver audit)
- **Effort:** Small (verification + tuning)

#### 2.6 Digital Power Monitoring
- **What:** INA226/228/238 I2C power monitor support
- **Why:** Analog ADC battery monitoring has no current calibration, no per-cell voltage
- **STM32 ref:** FMUv6X uses multiple INA2xx + ADS1115 with auto-detection
- **Files to modify:**
  - `default.px4board` (enable INA226 driver)
  - `rc.board_sensors` (start driver)
- **Effort:** Small (driver exists, just needs board config)
- **Demo board testable:** Yes (with I2C power monitor click board)

---

### TIER 3 — MEDIUM (Improves capability, not blocking)

#### 3.1 Ethernet / MAVLink UDP
- **What:** Enable SAMV7 GMAC for network telemetry
- **Why:** High-bandwidth telemetry, ROS2 bridge, multi-GCS
- **Hardware:** SAMV71-XULT has on-board Ethernet (KSZ8061 PHY)
- **NuttX:** `CONFIG_SAMV7_EMAC0` available
- **Effort:** Medium-Large
- **Demo board testable:** Yes (RJ45 on board)

#### 3.2 LED PWM Driver
- **What:** PWM-controlled RGB status LED
- **Why:** Richer status indication (breathing, color mixing)
- **NXP ref:** `led_pwm.cpp` using FTM timer channels
- **SAMV7 approach:** Use spare TC or PWMC channel for LED PWM
- **Effort:** Small-Medium

#### 3.3 HW Version Detection
- **What:** ADC-based hardware revision/version detection
- **Why:** Allows single firmware to support multiple PCB revisions
- **STM32 ref:** `board_hw_info/board_hw_rev_ver.c` — ADC reads resistor dividers
- **Effort:** Small (for custom PCB — not applicable to demo board)

#### 3.4 Temperature Compensation
- **What:** IMU/baro thermal calibration compensation
- **Why:** Reduces sensor drift across temperature range
- **STM32 ref:** `CONFIG_MODULES_TEMPERATURE_COMPENSATION=y`
- **Files to modify:** `default.px4board` (enable module)
- **Effort:** Small (module exists, needs board enable + calibration data)

#### 3.5 IMU Heater
- **What:** PWM-driven IMU heater for thermal stability
- **Why:** Keeps IMU at constant temperature for consistent performance
- **Needs:** Heater resistor near IMU + GPIO/PWM output
- **Demo board:** Not testable without hardware mod

#### 3.6 uXRCE-DDS (ROS2 Bridge)
- **What:** Micro XRCE-DDS agent for ROS2 integration
- **Why:** Enables ROS2 topic bridge for advanced autonomy
- **Files to modify:** `default.px4board` (enable module)
- **Effort:** Small (module exists)

#### 3.7 Automounter (SD Card Hot-Plug)
- **What:** Automatic SD card mount/unmount on insertion/removal
- **Why:** Robustness for in-field SD card swap
- **NXP ref:** FMUK66 has full automounter with debounce
- **Blocked:** Card detect GPIO (PD18) used for RC UART — not available on demo board
- **Effort:** Small (for custom PCB with CD pin)

---

### TIER 4 — LOW PRIORITY (Nice-to-have / Custom PCB only)

| Feature | Notes |
|---------|-------|
| 8-channel PWM | Custom PCB Phase 5 — needs PWMC1 or additional TC channels |
| PX4IO coprocessor | Requires separate STM32 on custom PCB |
| Expanded ADC (RSSI, 5V sense, etc.) | Custom PCB Phase 6 |
| RAMTRON FRAM | Alternative to QSPI flash params |
| Camera trigger/capture | Application-specific |
| Gimbal control | Application-specific |
| Airspeed sensor | Fixed-wing only |
| SE050 security element | Optional crypto |
| SPI RGB LED DMA | Niche |

---

## Summary: Implementable on Demo Board (Priority Order)

| # | Feature | Tier | Effort | Dependencies |
|---|---------|------|--------|-------------|
| 1 | Tone Alarm | CRITICAL | Medium | Piezo buzzer on GPIO |
| 2 | Hardfault Logging (QSPI/SD fallback) | CRITICAL | Small-Medium | None |
| 3 | NuttX sam_progmem.c __ramfunc__ fix | CRITICAL | Medium | NuttX patch |
| 4 | DShot ESC Protocol | CRITICAL | Large | Oscilloscope for validation |
| 5 | UART DMA | HIGH | Small | None |
| 6 | CAN/UAVCAN | HIGH | Medium | CAN transceiver click board |
| 7 | Board Peripheral Reset | HIGH | Small-Medium | None |
| 8 | RC Input Capture (TC5) | HIGH | Medium-Large | PPM receiver |
| 9 | Digital Power Monitor (INA226) | HIGH | Small | I2C power monitor |
| 10 | Bootloader | CRITICAL | Large | None |
| 11 | Ethernet / MAVLink UDP | MEDIUM | Medium-Large | On-board (available) |
| 12 | Temperature Compensation | MEDIUM | Small | Calibration procedure |
| 13 | LED PWM | MEDIUM | Small-Medium | LED on GPIO |
| 14 | uXRCE-DDS (ROS2) | MEDIUM | Small | None |
| 15 | SPI DMA audit | HIGH | Small | None |

---

## Cross-Platform Comparison Summary

| Feature | STM32 FMUv6X | NXP FMUK66 | NXP i.MX RT | SAMV71 (Rathi) |
|---------|-------------|-----------|------------|----------------|
| PWM Channels | 9 | 6 | 8 | 4 |
| DShot | Yes (DMA burst) | No | Yes | No (planned) |
| Tone Alarm | Yes (timer PWM) | Yes (TPM) | Yes | **No** |
| Hardfault Persist | BBSRAM | Alert only | SSARC | **Disabled** |
| Bootloader | Yes | Yes (VBAT magic) | Yes (ROM API) | **No** |
| CAN/UAVCAN | Yes (FDCAN) | Yes (FlexCAN x2) | Yes | **No** |
| Ethernet | Yes (LAN8742A) | Yes (TJA1100) | Yes | **No** (HW available) |
| I2C Buses | 4 | 2 | 4 | 1 |
| SPI Buses | 4 | 3 | 3+ | 1 |
| UART Ports | 8 | 5 | 6+ | 3 |
| IMU Redundancy | Triple | Dual | Triple | Single |
| Power Monitor | Digital (INA2xx) | Digital + Analog | Digital | **Analog only** |
| RC Capture | Timer + DMA | FTM capture | Timer | **UART only** |
| HW Versioning | ADC-based | No | ADC-based | **No** |
| LED PWM | Yes | Yes (FTM RGB) | Yes | **No** |
| ROS2 Bridge | Yes | Yes | Yes | **No** |
| Watchdog | Yes | No | Yes (S32K) | Yes |
| OneShot125 | Yes | No | Yes | Yes |
| QSPI Flash | SPI5 NOR | No (RAMTRON) | FlexSPI NOR | Yes (S25FL116K) |
| TRNG | Yes | No | Yes | Yes |
