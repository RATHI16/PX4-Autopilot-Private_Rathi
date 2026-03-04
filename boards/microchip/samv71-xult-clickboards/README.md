# SAMV71-XULT PX4 Flight Controller Port

**Status:** Phase 3 Complete — Hardware Verified, Flight-Test Ready | **Last Updated:** 2026-03-04

PX4 Autopilot port for the Microchip SAMV71-XULT evaluation board with Click sensor boards.

---

## Current State

The board boots reliably, passes all 21 unit tests, and is ready for flight testing with real hardware sensors. All critical boot bugs from the Phase 0-3 commit have been resolved.

**Build:** 1,405,924 B flash (76.62% of 1792 KB) | 52,020 B SRAM (15.88% of 320 KB)

### Working

- Full PX4 stack boot and operation (NuttX 11.0, PX4 v1.17.0)
- 4-channel PWMC PWM output (50-400Hz, dynamic prescaler, OneShot mode)
- IMU: ICM-45686 (SPI0) — 6DOF IMU 27 Click
- Barometers: DPS310 (I2C), BMP388 (I2C)
- Magnetometers: AK09916 (I2C), BMM150 (I2C)
- IMU: BMI088 accel+gyro (I2C) — 13DOF Click
- GPS serial input (UART2 / ttyS1)
- RC serial input with SBUS (UART4 / ttyS2, 100000 baud 8E2)
- Battery ADC monitoring (AFEC0: PD30=voltage, PA18=current)
- Safety button (SW0 on PA9) with LED1 (PC9) indicator
- USB CDC/ACM console and MAVLink (development dialect)
- SD card storage (params backup, flight logs)
- QSPI flash persistent storage (params, caldata, dataman/waypoints)
- HRT (high-resolution timer via TC0)
- EKF2 state estimation (with double-precision FPU support)
- Multicopter flight control stack (attitude, rate, position)
- Board unique ID (128-bit UID via EEFC STUI/SPUI, `__ramfunc__`)
- HITL simulation mode (SYS_AUTOSTART=1001, jMAVSim verified)
- Hardfault crash dump defines (storage temporarily disabled — see Known Issues)
- SD flight logging (SDLOG_MODE=0, SDLOG_PROFILE=1)
- QGroundControl full support (embedded metadata for Actuators tab)

### Known Issues

| Issue | Impact | Workaround | Future Fix |
|-------|--------|------------|------------|
| HAS_PROGMEM disabled | No crash dump storage | Crash dumps not captured | Patch NuttX `sam_progmem.c` with `__ramfunc__` |
| WDT disabled | No hardware watchdog | Board won't auto-reset on hang | Implement SAMV7 `watchdog_init()`/`watchdog_pet()` HAL |
| bmi088_i2c not built | BMI088 I2C driver unavailable | Use SPI variant or other IMU | Investigate CMake build issue |
| MAVLink USB contention | Can't use NSH + MAVLink on same port | Use J501 for NSH, J500 for MAVLink | Configure MAVLink on separate serial |

See **[CHANGELOG_POST_PHASE3.md](CHANGELOG_POST_PHASE3.md)** for detailed bug descriptions and root cause analysis.

### In Progress

- Tone alarm (GPIO toggle or TC PWM for piezo buzzer)
- DShot ESC protocol (PWMC Sync Mode + XDMAC — [plan exists](DSHOT_IMPLEMENTATION_PLAN.md))
- RC input capture via TC5 (PPM/PWM decode on PC29)
- SAMV7 watchdog HAL implementation
- NuttX sam_progmem.c `__ramfunc__` fix

### Planned (Custom PCB)

- 8-channel PWM (add PWMC1 module)
- Extended ADC and power monitoring
- CAN/DroneCAN (MCAN0 on PB3/PB2)
- PX4 bootloader port (flash-resident, GPBR0 boot mode)
- Hardware versioning (ADC-based board ID)

---

## Hardware

- **MCU:** ATSAMV71Q21B (ARM Cortex-M7, 300MHz CPU / 150MHz MCK, 2MB Flash, 384KB SRAM)
- **FPU:** Single + Double precision hardware floating point
- **Board:** SAMV71-XULT Evaluation Kit
- **Debugger:** On-board EDBG (CMSIS-DAP)
- **Sensors:** MikroElektronika Click boards via mikroBUS sockets and Xplained Pro extension headers

### Supported Click Board Sensors

| Sensor | Type | Interface | Click Board | Status |
|--------|------|-----------|-------------|--------|
| ICM-45686 | IMU (6-DOF) | SPI | MIKROE-6514 (6DOF IMU 27) | Primary IMU |
| AK09916 | Magnetometer | I2C | MIKROE-4231 (Compass 4) | Verified |
| BMM150 | Magnetometer | I2C | MIKROE-2935 (GeoMagnetic) | Verified |
| DPS310 | Barometer | I2C | MIKROE-2293 (Pressure 3) | Verified |
| BMP388 | Barometer | I2C | MIKROE-3566 (Pressure 5) | Via I2C (was SPI) |
| BMI088 | IMU (6-DOF) | I2C | MIKROE-3775 (13DOF) | Build issue |

### Pin Mapping (Key Assignments)

| Function | Pin | Peripheral | Notes |
|----------|-----|------------|-------|
| Motor 1 | PC13 | PWMC0 CH3 | EXT2 Pin 4 |
| Motor 2 | PA2 | PWMC0 CH1 | EXT2 Pin 9 |
| Motor 3 | PC19 | PWMC0 CH2 | EXT2 Pin 7 |
| Motor 4 | PB0 | PWMC0 CH0 | EXT1 Pin 13 (was MB2 RST) |
| IMU CS | PD27 | SPI0 | ICM-45686 |
| IMU DRDY | PD28 | GPIO IRQ | ICM-45686 interrupt |
| GPS TX/RX | PD25/PD26 | UART2 | ttyS1 @ 57600 |
| RC SBUS | PD19 | UART4 | ttyS2 @ 100000 8E2 |
| Console | — | USART1 | ttyS0 @ 115200 |
| Safety | PA9 | GPIO | SW0, active LOW |
| Safety LED | PC9 | GPIO | LED1, active LOW |
| Batt Voltage | PD30 | AFEC0 CH0 | ADC input |
| Batt Current | PA18 | AFEC0 CH7 | ADC input |

---

## Building

```bash
# Full clean build (required after defconfig/metadata changes)
make clean && make microchip_samv71-xult-clickboards_default

# Incremental build
make microchip_samv71-xult-clickboards_default
```

## Flashing

```bash
# Flash via OpenOCD (EDBG CMSIS-DAP debugger)
openocd -f interface/cmsis-dap.cfg -f target/atsamv.cfg \
  -c "init; reset halt; flash write_image erase \
  build/microchip_samv71-xult-clickboards_default/microchip_samv71-xult-clickboards_default.bin \
  0x00400000; reset run; shutdown"
```

## Console Access

```bash
# NSH console via USB (J501 port)
picocom -b 115200 /dev/ttyACM0

# Run all unit tests
nsh> tests all

# Check sensor status
nsh> listener sensor_accel
nsh> listener sensor_baro
nsh> listener sensor_mag
```

---

## HITL (Hardware-In-The-Loop) Testing

```bash
# Set HITL mode
nsh> param set SYS_AUTOSTART 1001
nsh> reboot

# Run jMAVSim on host
./jmavsim_run.sh -d /dev/ttyACM1 -b 921600 -r 250
```

See **[HITL_TESTING_GUIDE.md](HITL_TESTING_GUIDE.md)** for full setup instructions.

---

## QSPI Flash Layout

| Partition | Path | Size | Purpose |
|-----------|------|------|---------|
| 0 | /fs/mtd_params | 128 KB | System parameters (BSON) |
| 1 | /fs/mtd_caldata | 64 KB | Factory calibration backup |
| 2 | /fs/mtd_waypoints | 512 KB | Dataman (missions, geofence, rally) |
| Reserved | — | 1344 KB | Future use |

---

## Serial Port Map

| Device | Peripheral | Function | Baud |
|--------|------------|----------|------|
| /dev/ttyS0 | USART1 | NSH Console | 115200 |
| /dev/ttyS1 | UART2 | GPS | 57600 |
| /dev/ttyS2 | UART4 | RC SBUS Input | 100000 |
| /dev/ttyACM0 | USB CDC | MAVLink / NSH | — |

**Note:** UART0 is disabled to free PA9 (Safety) and PB0 (Motor 4).

---

## Documentation Index

### Change History
| Document | Purpose |
|----------|---------|
| **[Post-Phase 3 Changelog](CHANGELOG_POST_PHASE3.md)** | **Latest: Bug fixes and features after Phases 0-3 commit** |
| [Known Issues](KNOWN_ISSUES.md) | Comprehensive issue tracker (16 issues) |
| [Full Gap Analysis](FULL_PORT_GAP_ANALYSIS.md) | SAMV71 vs FMUv6X vs FMUK66 comparison |

### Planning & Status
| Document | Purpose |
|----------|---------|
| [Production Plan](PRODUCTION_PLAN.md) | Active roadmap: Phases 0-7 |
| [Production Delta vs FMUv6X](PRODUCTION_DELTA_FMUv6X.md) | Gap analysis against FMUv6X reference |
| [DShot Implementation Plan](DSHOT_IMPLEMENTATION_PLAN.md) | PWMC Sync Mode + XDMAC design |
| [DShot Phase 2 Build Plan](DSHOT_PHASE2_BUILD_PLAN.md) | Step-by-step DShot build plan |

### Technical Implementation
| Document | Purpose |
|----------|---------|
| [SAMV71 Pin Map](SAMV71Q21B_PX4_DRONE_PIN_MAPPING.md) | Complete pin allocation reference |
| [IO Timer Allocation API](IO_TIMER_ALLOCATION_API.md) | Timer/PWM architecture reference |
| [PWMC Implementation](PWMC_IMPLEMENTATION.md) | PWM controller driver details |
| [QSPI Flash](QSPI_FILESYSTEM.md) | QSPI flash driver and partition design |

### Testing & Validation
| Document | Purpose |
|----------|---------|
| [HITL Testing Guide](HITL_TESTING_GUIDE.md) | Hardware-in-the-loop setup and testing |
| [Click Board Validation](CLICK_BOARD_VALIDATION_GUIDE.md) | Sensor board testing guide |

---

## License

BSD 3-Clause License - See repository root LICENSE file.
