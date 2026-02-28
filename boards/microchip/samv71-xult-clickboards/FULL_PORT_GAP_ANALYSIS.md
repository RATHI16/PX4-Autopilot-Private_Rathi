# PX4 Full Port Feature Gap Analysis: SAMV71 vs STM32 FMUv6X vs NXP FMUK66

> **Generated**: 2026-02-28
> **Branch**: `samv7-custom`
> **Purpose**: Comprehensive feature-by-feature comparison to identify all missing features needed for a production-ready SAMV71 PX4 port.

---

## Summary

The SAMV71 port has the **core flight stack working** (IMU, Baro, Mag, PWM, RC, HITL verified), but is missing **15+ features** that the reference implementations provide. The SAMV7 silicon has all the required peripherals (MCAN, GMAC, WDT, XDMAC) — the gaps are software/driver effort, not hardware limitations.

**Current estimated feature parity: ~45% vs FMUv6X**

---

## Platform Overview

| Property | STM32 FMUv6X | NXP FMUK66-V3 | SAMV71-XULT |
|----------|-------------|---------------|-------------|
| MCU | STM32H753II | MK66FN2M0VMD18 | ATSAMV71Q21B |
| Core | Cortex-M7 | Cortex-M4F | Cortex-M7 |
| CPU clock | 480 MHz | 168 MHz | 300 MHz |
| Peripheral clock | 240 MHz | 56 MHz (bus) | 150 MHz (MCK) |
| Flash | 2 MB | 2 MB | 2 MB |
| SRAM | 240 KB + 4 regions | 256 KB | 384 KB (320K + 64K nocache) |
| FPU | Single + Double | Single only | Single + Double |
| D-cache / I-cache | Yes / Yes | No / No | Yes (write-through) / Yes |
| Board ID | 53 | 28 | 1371 |

---

## 1. Motor Output

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 (Current) | Gap |
|---------|-------------|------------|-------------------|-----|
| PWM channels | 8 (TIM5/4/12) | 6 (FTM0/3) | **4 (PWMC0)** | Need 4+ more for custom PCB (Phase 5) |
| DShot | Yes — TIM DMA burst | No (FTM can't) | **No** — Phase 2 planned | **HIGH PRIORITY** — PWMC Sync + XDMAC |
| DShot bidirectional | Yes (eRPM telemetry) | No | No | Phase 2 extension |
| OneShot125 | Yes | Yes (FTM prescaler) | **No** | Add to io_timer_pwmc.c |
| Input capture | TIM1_CH2 (PE11) | FTM0_CH2 | **No** — PC29/TIOA5 reserved | Phase 3 (TC5 capture) |
| PWM input (PWMIN) | TIM4_CH2 | FTM2_CH2 | **No** | Needs TC-based implementation |
| PPM input | TIM8_CH1 via HRT | TPM1_CH1 via HRT | **No** — reserved | Phase 3 |
| PX4IO co-processor | Yes (USART6 1.5Mbps) | No | **No** | Not planned (direct FMU output) |

### Motor Pin Mapping (Current)

| PX4 Channel | Pin | PWMC | Peripheral |
|-------------|-----|------|------------|
| Motor 1 (ch0) | PC13 | PWMC0_H3 | Peripheral B |
| Motor 2 (ch1) | PA2 | PWMC0_H1 | Peripheral A |
| Motor 3 (ch2) | PC19 | PWMC0_H2 | Peripheral B |
| Motor 4 (ch3) | PB0 | PWMC0_H0 | Peripheral A |

### DShot Implementation Path (Phase 2)

- STM32 uses TIM Update DMA to burst CCR values — **not available on SAMV7 PWMC**
- SAMV7 PWMC has DMAR register in Sync Channel Mode (SCM.UPDM=2) — XDMAC writes to DMAR, hardware distributes to synchronized channels
- CPRE=1 (MCK/2=75MHz): DShot150 CPRD=500, DShot600 CPRD=125
- NXP K66 FTM **cannot do DShot at all** — SAMV71 would be ahead of FMUK66
- Plan doc: `DSHOT_PHASE2_BUILD_PLAN.md`

---

## 2. Bootloader

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Bootloader | Yes — app at 0x08020000 | Yes — pre-built binary | **None** | **HIGH PRIORITY** |
| Firmware update over USB | Yes | Yes | No | Requires bootloader |
| UAVCAN/DroneCAN update | Yes | Yes | No | Requires CAN + bootloader |

### What's Needed

- PX4 bootloader port for SAMV7 (SAM-BA ROM bootloader exists in silicon for emergency, but PX4 bootloader needed for standard update flow)
- Flash layout: reserve first N sectors for bootloader, app starts at offset
- Board reset to bootloader support (`board_reset.cpp` already exists)
- Linker script update: move app origin from `0x00400000` to `0x00400000 + bootloader_size`

---

## 3. Non-Volatile Storage

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Parameter storage | SPI FRAM (FM25V02A, 32KB) | SPI FRAM (SPI0) | QSPI NOR (S25FL116K) | Working, different approach |
| Caldata storage | I2C EEPROM (24LC64T) | FRAM partition | QSPI partition | Working |
| Dataman/waypoints | SD card | SD card | QSPI partition | Working |
| BBSRAM crashdump | Yes (5 slots, battery-backed) | No | **No** | See hardfault section |
| Network config | I2C EEPROM partition | N/A | N/A | N/A (no Ethernet) |
| HW version EEPROM | I2C EEPROM | No | No | Phase 5 custom PCB |

### SAMV71 QSPI Flash Layout (S25FL116K, 2MB)

| Partition | Size | Mount Point |
|-----------|------|-------------|
| Params | 128 KB (32 sectors) | `/fs/mtd_params` |
| Caldata | 64 KB (16 sectors) | `/fs/mtd_caldata` |
| Waypoints | 512 KB (128 sectors) | `/fs/mtd_waypoints` |

QSPI-based approach is **actually better** than FMUK66's for wear leveling. No gap here.

---

## 4. Communication Buses

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| UARTs | 8 (USART1-3, UART4-5, USART6, UART7-8) | 5 (LPUART0, UART0-2,4) | **4** (USART1, UART0,2,4) | Need more on custom PCB |
| UART DMA RX | Yes (selected ports) | Yes (all 4 UARTs) | **No** — polled | **MEDIUM** — add XDMAC RX |
| UART inversion | Yes (STM32 HW invert) | Yes (Kinetis HW invert) | **No** | Needed for SBUS without inverter |
| UART singlewire | Yes (half-duplex) | No | No | Low priority |
| SPI buses | 6 (SPI0-6) | 3 (SPI0-2) | **2** (SPI0, QSPI) | Phase 5 custom PCB |
| I2C buses | 4 (I2C1-4) | 2 (I2C0-1) | **1** (TWIHS0) | Phase 5 custom PCB |
| CAN / FDCAN | 2x FDCAN | 2x FlexCAN | **None** | **HIGH PRIORITY** — MCAN0/1 available |
| DroneCAN/UAVCAN | Full stack | Full stack (v0 + v1) | **None** | Requires CAN HW + SW |
| Ethernet | Yes (LAN8742A, 100BASE-TX) | Yes (TJA1100, 100BASE-T1) | **No** | SAMV7 has GMAC — Phase 5+ |

### SAMV71 UART Map (Current)

| NuttX Device | Peripheral | PX4 Role | Baud |
|---|---|---|---|
| `/dev/ttyS0` | USART1 (PA21/PA22) | NSH console | 115200 |
| `/dev/ttyS1` | UART0 (PA9/PA10) | TEL2 | 115200 |
| `/dev/ttyS2` | UART2 (PD25/PD26) | GPS1 | 57600 |
| `/dev/ttyS3` | UART4 (PD18) | RC Input | 115200 |

### CAN / DroneCAN Implementation (Missing)

SAMV71 has **two MCAN controllers** (MCAN0, MCAN1) in silicon — they just need enabling:

**NuttX defconfig additions:**
```
CONFIG_SAMV7_MCAN0=y
CONFIG_SAMV7_MCAN1=y
CONFIG_CAN=y
```

**PX4 side:**
- Port the UAVCAN driver (or enable SocketCAN + Cyphal)
- Pin assignment: MCAN0 (PC12/PD28), MCAN1 (PD12/PC14) — check conflicts
- Need CAN transceiver hardware (TJA1050 or similar)

---

## 5. Tone Alarm / Buzzer

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Tone alarm | TIM14_CH1 PWM (PF9) | TPM2_CH1 (PTA11) | **Not implemented** | **MEDIUM PRIORITY** |
| tune_control cmd | Yes | Yes | Built but no HW output | Need PWM-based driver |

### What's Needed

- Pick a TC channel (TC1/TC2 have unused channels)
- Write `ToneAlarmInterface.cpp` for SAMV7 TC PWM output
- Wire a piezo buzzer to the selected pin
- `CONFIG_SYSTEMCMDS_TUNE_CONTROL=y` already built

---

## 6. LED System

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Status LEDs | 3 GPIO (R/G/B) | 2 GPIO (D9/D10) | 1 GPIO (PA23 yellow) | **Need RGB LEDs** on custom PCB |
| LED PWM driver | Yes (io_timer based) | Yes (FTM3-based) | **No** | Need PWMC or TC PWM driver |
| External RGB LED | I2C (NCP5623C) | I2C (NCP5623C built) | **No** | Add I2C RGB module |
| NeoPixel/WS2812 | SPI DMA (srgbled_dma) | No | No | Low priority |
| Safety LED | PD10 GPIO | PTC0 GPIO | PC9 GPIO | **Working** |

---

## 7. Watchdog

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Hardware watchdog | IWDG (NuttX framework) | WDOG (NuttX framework) | **Not configured** | **HIGH PRIORITY** |

### What's Needed

```
# defconfig additions:
CONFIG_WATCHDOG=y
CONFIG_SAMV7_WDT=y
CONFIG_SAMV7_WDT_INTERRUPT=y  # optional
```

SAMV7 has a 12-bit WDT with configurable timeout. NuttX driver exists (`arch/arm/src/samv7/sam_wdt.c`).

---

## 8. Hardfault Logging

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Hardfault alert | Yes | Yes | **Yes** (enabled) | OK |
| Stack dump | Yes | Yes | **Yes** | OK |
| Crashdump to storage | BBSRAM (5 slots, survives reset) | SD card | **SD card only** | Partial |
| hardfault_log command | Yes (streams via MAVLink) | No | **No** | **MEDIUM** |
| Board CRASHDUMP | Yes | Yes | **Yes** | OK |
| Reset on assert | Yes (mode 2) | Yes (mode 2) | **Yes** (mode 2) | OK |

### What's Needed for Parity

- `hardfault_log` command requires `HAS_PROGMEM` infrastructure
- Need to implement PROGMEM for SAMV7 internal flash, or use a QSPI partition for crashdump retention
- Alternative: dedicate a small QSPI partition as pseudo-BBSRAM for crash data

---

## 9. Board Monitoring

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| board_critmon | DWT cycle counter (real) | Real implementation | **Stub only** | **LOW** — implement with DWT |
| CPU load monitoring | load_mon module | load_mon module | **Working** | OK |
| Sched instrumentation | External + switch | External + switch | External + switch | OK |

### What's Needed

- SAMV71 Cortex-M7 has DWT — same as STM32H7
- Just enable `DWT->CYCCNT` in `board_critmon.c` (currently a stub)

---

## 10. Power Management

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| Power brick detection | 2 bricks (GPIO valid) | 1 brick (hardcoded) | **1 brick (hardcoded)** | OK for dev board |
| Battery ADC | AFEC-based V+I | ADC1 V+I | **AFEC0 V+I** | Working |
| 5V rail control | GPIO enable + OC sense | No | **No** | Custom PCB (Phase 5) |
| Sensor rail control | GPIO enable (4 rails) | PTB8 enable (1 rail) | **No** | Custom PCB |
| Heater | TIM2_CH3 PWM | No | **No** | Low priority |
| INA226/228 power mon | I2C (auto-detected) | INA226 (built) | **No** | Add for custom PCB |

---

## 11. HW Versioning

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| HW version detection | ADC3 (7 variants!) | No | **No** | Phase 5 custom PCB |
| Board UUID | STM32 UID (96-bit) | K66 UID | **CHIPID (128-bit)** | Working |
| Sensor variant tables | 7 SPI config tables | Fixed config | **Fixed config** | OK for now |

---

## 12. RC Input

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| SBUS (UART inverted) | HW invert on USART6 | HW invert on UART1 | **External inverter** | Works, but HW invert cleaner |
| DSM/Spektrum | Singlewire half-duplex | UART | UART | OK |
| PPM | TIM8_CH1 via HRT | TPM1_CH1 via HRT | **No** | Phase 3 (TC5 capture) |
| RSSI ADC | ADC channel | ADC1_SE13 | **No** | Add ADC channel |

---

## 13. Security

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| SE050 security element | I2C4 0x48 | No | **No** | Low priority |
| TRNG | Yes | Yes (Kinetis RNGA) | **Yes** (TRNG enabled) | OK |
| HW CRC | Yes | Yes (Kinetis CRC) | **No** | Low priority |

---

## 14. Debug / Development

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Gap |
|---------|-------------|------------|--------|-----|
| SWD/JTAG debug | Yes | Yes (SWD via DCD-Mini) | **Yes** (J-Link) | OK |
| NSH console | USART3 | LPUART0 (57600) | USART1 (115200) | OK |
| i2cdetect | Yes | Yes | **Yes** | OK |
| GPIO command | Yes (STM32-specific) | No | **No** | Need SAMV7-specific `gpio` cmd |
| sd_bench | Yes | Yes | **No** (not built) | Add to px4board |
| serial_test | Yes | Yes | **No** | Low priority |
| netman | Yes | Yes | No (no Ethernet) | N/A |

---

## 15. USB

| Feature | STM32 FMUv6X | NXP FMUK66 | SAMV71 | Status |
|---------|-------------|------------|--------|--------|
| USB device | OTG FS | OTG FS | **USBDEVHS** | Working |
| CDC/ACM | Yes (PID=0x0035) | Yes (PID=0x001c) | **Yes** (PID=0x1371) | Working |
| MAVLink on USB | Yes | Yes | **Yes** (`/dev/ttyACM0`) | Working |
| VBUS detection | PA9 GPIO | PTE8 GPIO + ADC | **N/A** | OK |

---

## Priority Implementation Roadmap

### Immediate (Before Next Flight)

| # | Task | Effort | Notes |
|---|------|--------|-------|
| 1 | **Restore PID gains** | 5 min | Currently ALL ZEROED — critical safety fix |
| 2 | **Remove debug flags** | 5 min | `CONFIG_DEBUG_FS_INFO`, `CONFIG_SAMV7_HSMCI_CMDDEBUG` causing 40ms SD writes |
| 3 | **Fix NAV_RCL_ACT** | 1 min | Currently 6 (Terminate/kill motors) -> change to 2 (Return) |

### High Priority (Full Port Features)

| # | Task | Effort | Notes |
|---|------|--------|-------|
| 4 | **DShot output** | 2-3 weeks | Phase 2: PWMC Sync Mode + XDMAC (plan exists) |
| 5 | **Bootloader** | 1-2 weeks | Port PX4 bootloader for SAMV7 |
| 6 | **Hardware watchdog** | 1 day | Enable `CONFIG_SAMV7_WDT=y` (NuttX driver exists) |
| 7 | **CAN/DroneCAN** | 2-3 weeks | Enable MCAN0/1, port UAVCAN driver |

### Medium Priority (Production Hardening)

| # | Task | Effort | Notes |
|---|------|--------|-------|
| 8 | **Tone alarm** | 3-5 days | TC-based PWM buzzer driver |
| 9 | **Hardfault log** | 1 week | QSPI partition for crash retention + `hardfault_log` cmd |
| 10 | **OneShot125** | 2-3 days | Add to io_timer_pwmc.c |
| 11 | **Input capture** | 1 week | Phase 3: TC5 for PPM/RC capture |
| 12 | **UART DMA RX** | 1 week | XDMAC receive for serial ports |
| 13 | **board_critmon** | 1 day | Enable DWT cycle counter (Cortex-M7 has it) |
| 14 | **LED PWM** | 3-5 days | RGB status LEDs (needs custom PCB) |

### Custom PCB Phase (Phase 5-7)

| # | Task | Notes |
|---|------|-------|
| 15 | **8-channel PWM** | Expand from 4 to 8 output channels |
| 16 | **Additional SPI/I2C** | More sensor buses |
| 17 | **Ethernet** | SAMV7 GMAC + PHY |
| 18 | **HW versioning** | ADC-based board ID |
| 19 | **Power rail control** | GPIO-controlled sensor/peripheral power |
| 20 | **Additional UARTs** | More serial ports for telemetry/GPS2 |

---

## Feature Count Summary

| Category | FMUv6X | FMUK66 | SAMV71 | SAMV71 % Complete |
|----------|--------|--------|--------|-------------------|
| Motor output | 8ch + DShot + PX4IO | 6ch + OneShot | 4ch PWM only | ~40% |
| Communication | 8 UART, 2 CAN, Ethernet | 5 UART, 2 CAN, Ethernet | 4 UART, no CAN, no Eth | ~35% |
| Storage | FRAM + 2 EEPROM + SD | FRAM + SD | QSPI + SD | ~80% |
| Sensors | 3x IMU, Baro, Mag (7 HW variants) | 1x IMU, Baro, Mag | 1x IMU, Baro, Mag | ~70% |
| Safety/monitoring | Watchdog, BBSRAM, critmon, tone | Watchdog, tone, LED PWM | Safety button only | ~25% |
| Boot/update | Bootloader + USB + UAVCAN | Bootloader + USB | None | ~0% |
| Power mgmt | 2 bricks, 4 rails, heater | 1 brick | 1 brick (hardcoded) | ~50% |
| **Overall** | **Reference (100%)** | **~75%** | **~45%** | — |

---

## Key Takeaway

The SAMV71 port is at roughly **45% feature parity** with FMUv6X. The biggest gaps are:

1. **DShot** (Phase 2 planned, PWMC Sync + XDMAC)
2. **Bootloader** (no firmware update mechanism)
3. **CAN/DroneCAN** (MCAN silicon available, needs driver work)
4. **Watchdog** (NuttX driver exists, just needs defconfig enable)

The good news: the SAMV7 silicon has **all the required peripherals** (MCAN, GMAC, WDT, XDMAC, DWT) — every gap is a software/driver effort, not a hardware limitation.

---

## Known Pin Conflicts

| Pin | Conflict A | Conflict B | Resolution |
|-----|-----------|-----------|------------|
| PA9 | Safety Button (SW0) | UART0_RXD | Currently safety button wins |
| PB0 | Motor 4 (PWMC0_H0) | UART0_TXD | Currently motor wins |
| PD18 | SD Card Detect | TC5 / UART4 RC | CD disabled in Rathi's fork, used for RC |
| PD28 | IMU DRDY (SPI0) | MCAN0_RX | Conflict if CAN enabled — needs remap |

---

## References

- Production delta doc: `PRODUCTION_DELTA_FMUv6X.md`
- DShot plan: `DSHOT_PHASE2_BUILD_PLAN.md`
- HITL guide: `HITL_TESTING_GUIDE.md`
- IO Timer API: `IO_TIMER_ALLOCATION_API.md`
- FMUv6X board: `boards/px4/fmu-v6x/`
- FMUK66 board: `boards/nxp/fmuk66-v3/`
- SAMV7 HAL: `platforms/nuttx/src/px4/microchip/samv7/`
