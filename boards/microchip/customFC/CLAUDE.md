# Custom Flight Controller - SAMV71Q21B / PIC32CZ CA70 (EV61G06A)

## Board Overview
- **MCU:** ATSAMV71Q21B or PIC32CZ2051CA70144 (pin-compatible, ARM Cortex-M7, 300MHz, 2MB Flash, 384KB SRAM)
- **Package:** LQFP-144
- **Part Number:** EV61G06A
- **PCB:** Custom drone flight controller designed in Altium
- **Branch:** `SAMV71_FCV1` (production custom FC, differs from old `samv7-custom` EVB branch)

## What Changed from Old EVB Branch (`samv7-custom`)

| Feature | Old Branch (samv7-custom / EVB) | New Branch (SAMV71_FCV1 / Custom FC) |
|---------|-------------------------------|--------------------------------------|
| **Clock source** | Crystal (MOSCXTEN) | **MEMS Oscillator DSC6011 (MOSCXTBY bypass)** |
| **Clock init** | Standard NuttX | **Patched sam_clockconfig.c (timeout on MOSCXTS)** |
| **LEDs** | PA23 (single LED) | **PC17 (blue), PA0 (amber), PD10 (red)** |
| **Safety button** | PA9 | **PE4** |
| **Safety LED** | PC9 | **PE3** |
| **SPI0 IMU CS** | PA11 (ICM-20689) | **PD12 (ICM-45686)** |
| **SPI1 IMU2** | Not used | **PC25 CS, PC24 SCK, PC26 MISO, PC27 MOSI** |
| **I2C buses** | 1 bus (TWIHS0) | **2 buses (TWIHS0 + TWIHS2)** |
| **CAN** | Not configured | **PB2/PB3/PA29 (ATA6563)** |
| **Power control** | Not present | **PB13, PE1, PE2, PD29, PB5** |
| **QSPI flash** | S25FL116K (W25 driver) | **SST26VF032B (SST26 driver, JEDEC bf 26 42)** |
| **GPS baud** | 38400 | **115200 (u-blox NEO-M9N)** |
| **RC pin** | PD18 (UART4) | **PA5 (UART1)** |
| **ttyS ordering** | Different (4 UARTs) | **5 UARTs enabled (see below)** |
| **Motor channels** | 4 (PWMC only) | **8 (4 PWMC + 4 TC)** |
| **DShot** | Not implemented | **DShot300 on all 8 channels** |
| **USART1 parity** | Default (PAR=0 = EVEN) | **PAR=4 (NO parity) — critical fix** |
| **Boot indicator** | None | **PA0 blinks 3x on boot** |
| **UART1 TXD** | Not defined | **GPIO_UART1_TXD = GPIO_UART1_TXD_3 (PA6)** |

## Clock Configuration (CRITICAL - DSC6011 Bypass Mode)
- **Main Oscillator:** DSC6011JI2B-012.0000 (12 MHz MEMS, CMOS output) — NOT a crystal!
- **Mode:** `BOARD_CKGR_MOR_MOSCXTENBY = PMC_CKGR_MOR_MOSCXTBY` (bypass, no MOSCXTEN)
- **Patch Required:** `sam_clockconfig.c` must have timeout on MOSCXTS wait:
  ```c
  #if defined(BOARD_CKGR_MOR_MOSCXTENBY) && \
      (BOARD_CKGR_MOR_MOSCXTENBY & PMC_CKGR_MOR_MOSCXTBY)
      volatile int timeout = 1000;
      while (!(getreg32(SAM_PMC_SR) & PMC_INT_MOSCXTS) && --timeout > 0);
  #else
      sam_pmcwait(PMC_INT_MOSCXTS);
  #endif
  ```
- **Without this patch:** MCU hangs during boot, LED stays solid, no UART output
- **PLL:** 12 MHz × 25 = 300 MHz (CPU), MCK = 150 MHz
- **Slow Clock:** 32.768 kHz crystal (Y2) — for RTC only, not critical for boot

## ttyS Port Ordering (VERIFIED WORKING)

NuttX assigns ports in this fixed order: Console first, then UART0→UART4, then USART0→USART2.

**Enabled peripherals in defconfig:**
```
CONFIG_SAMV7_USART1=y  (console)
CONFIG_SAMV7_UART0=y
CONFIG_SAMV7_UART1=y
CONFIG_SAMV7_UART2=y
CONFIG_SAMV7_USART2=y
```

**Resulting assignment:**
| Device | Peripheral | Pins | Baud | Config | Function |
|--------|-----------|------|------|--------|----------|
| `/dev/ttyS0` | USART1 | PA21(RX), PB4(TX) | 115200 8N1 | Console | Debug/NSH |
| `/dev/ttyS1` | UART0 | PA9(RX), PA10(TX) | 57600 8N1 | `SERIAL_TEL2` | Telemetry 2 (SiK radio) |
| `/dev/ttyS2` | UART1 | PA5(RX only) | 100000 8E2 | `SERIAL_RC` | RC Input (SBUS) |
| `/dev/ttyS3` | UART2 | PD25(RX), PD26(TX) | 115200 8N1 | `SERIAL_GPS1` | GPS (u-blox M9N) |
| `/dev/ttyS4` | USART2 | PD15(RX), PD16(TX) | 57600 8N1 | `SERIAL_TEL3` | Telemetry 1 (w/ flow ctrl) |
| `/dev/ttyACM0` | USB CDC/ACM | HSDP/HSDM | 2000000 | `SERIAL_TEL1` | MAVLink to QGC |

**px4board config:**
```
CONFIG_BOARD_SERIAL_GPS1="/dev/ttyS3"
CONFIG_BOARD_SERIAL_TEL1="/dev/ttyACM0"
CONFIG_BOARD_SERIAL_TEL2="/dev/ttyS1"
CONFIG_BOARD_SERIAL_TEL3="/dev/ttyS4"
CONFIG_BOARD_SERIAL_RC="/dev/ttyS2"
```

**WARNING:** If you enable/disable ANY UART in defconfig, all ttyS numbers SHIFT. Always verify with `ls /dev/` after changes.

## Programming
- **Interface:** SWD (SWDIO=PB6, SWCLK=PB7)
- **Connector:** J2 (1x6 pin header)
- **Tool:** PICkit 4 via MPLAB X IDE (NOT IPE — IPE holds reset)
- **Device Selection:**
  - For SAMV71: Select **ATSAMV71Q21B**
  - For PIC32CZ CA70: Select **PIC32CZ2051CA70144**
- **MPLAB IPE "Hold in Reset" workaround:** Disconnect PICkit NRST wire after programming, then power cycle
- **Hardware Erase:** Short PB12 to 3.3V → power cycle → wait 3s → remove jumper
- **Build & Flash:**
  ```bash
  cd ~/Drone_Rathi/PX4-Autopilot-Private_Rathi
  bash build_flash.sh
  # Then flash hex from C:\Users\I73780\ via MPLAB
  ```

## Debug Console
- **USART1:** PA21 (RX), PB4 (TX) — J1 header (4-pin: GND, PA21, PB4, VCC)
- **Baud:** 115200 8N1
- **IMPORTANT:** USART Mode Register PAR field must be 0x800 (NO parity, PAR=4). Default PAR=0 means EVEN parity on SAMV7!

## Pin Mapping

### Motors (8 channels — 4 PWMC + 4 TC)
| Motor | Pin  | Peripheral | Timer Index | DShot Method |
|-------|------|-----------|-------------|--------------|
| M1 | PB0 | PWMC0 CH0 (PerA) | io_timers[0] | XDMAC DMA |
| M2 | PA2 | PWMC0 CH1 (PerA) | io_timers[0] | XDMAC DMA |
| M3 | PC19 | PWMC0 CH2 (PerB) | io_timers[0] | XDMAC DMA |
| M4 | PC13 | PWMC0 CH3 (PerB) | io_timers[0] | XDMAC DMA |
| M5 | PA15 | TC0 CH1 TIOA1 (PerB) | io_timers[1] | ISR-driven |
| M6 | PC23 | TC1 CH0 TIOA3 (PerB) | io_timers[2] | ISR-driven |
| M7 | PC29 | TC1 CH2 TIOA5 (PerB) | io_timers[3] | ISR-driven |
| M8 | PC5 | TC2 CH0 TIOA6 (PerB) | io_timers[4] | ISR-driven |

### SPI Buses
| Bus | Function | CS | SCK | MOSI | MISO | INT | Periph |
|-----|----------|-----|------|------|------|-----|--------|
| SPI0 | IMU1 (ICM-45686) | PD12 (GPIO) | PD22 | PD21 | PD20 | PC2 | Periph B |
| SPI1 | IMU2 (ICM-45686) | PC25 (GPIO) | PC24 | PC27 | PC26 | PC3 | Periph C |
| QSPI | Flash (SST26VF032B) | PA11 | PA14 | PA13 | PA12 | — | — |

### I2C Buses
| Bus | NuttX | Function | SDA | SCL | Devices |
|-----|-------|----------|------|------|---------|
| I2C0 | TWIHS0 (bus 1 in PX4) | Sensors | PA3 | PA4 | BMP388 @0x77, BMM150 @0x10, EEPROM @0x50 |
| I2C2 | TWIHS2 (bus 3 in PX4) | External | PD27 | PD28 | GPS mag, Power sensor, Crypto (TA101T) |

### Serial Ports (see ttyS table above)

### CAN
| Bus | TX | RX | STB | Transceiver |
|-----|-----|-----|------|-------------|
| CAN0 | PB2 | PB3 | PA29 (LOW=active) | ATA6563 |

### SD Card (HSMCI)
| Signal | Pin |
|--------|------|
| MCCK | PA25 |
| MCCDA | PA28 |
| MCDA0 | PA30 |
| MCDA1 | PA31 |
| MCDA2 | PA26 |
| MCDA3 | PA27 |
| MCCD | PA24 (not used — always present) |

### LEDs (active LOW)
| LED | Pin | Color | Function |
|-----|------|-------|----------|
| LED1 | PC17 | Blue | Status/Armed |
| LED2 | PA0 | Amber | Boot indicator (3 blinks) |
| LED3 | PD10 | Red | Error |

### Power Control
| Function | Pin | Active |
|----------|------|--------|
| 3.3V Periph EN | PB13 | HIGH = enabled |
| 3.3V Periph FLT | PE0 | LOW = fault |
| 5V Periph EN | PE1 | HIGH = enabled |
| 5V Periph FLT | PE2 | LOW = fault |
| 5V Tele EN | PD29 | HIGH = enabled |
| 5V Tele FLT | PB5 | LOW = fault |

### Safety
| Function | Pin |
|----------|------|
| Safety Button | PE4 (active LOW, internal pull-up) |
| Safety LED | PE3 (active LOW) |

### USB
- USB D+: HSDP (pin 137)
- USB D-: HSDM (pin 136)
- Type-C connector (J16)

## Sensor Configuration (rc.board_sensors)

```bash
# ICM-45686 IMU2 on SPI bus 2 (SPI1) — via J11 connector
icm45686 -s -b 2 start

# BMM150 Magnetometer on I2C bus 1 at address 0x10
bmm150 -X -b 1 start

# BMP388 Barometer on I2C bus 1 at address 0x77 (119 decimal)
bmp388 -X -b 1 -a 119 start
```

**Note:** IMU1 (SPI0) disabled — DF40C board-to-board connector has MISO contact issue. IMU2 works via J11 wire connector.

## QSPI Flash — SST26VF032B (NOT S25FL/W25)

- **JEDEC ID:** 0xBF 0x26 0x42
- **Driver:** `CONFIG_MTD_SST26=y` (not W25!)
- **API:** `sst26_initialize_spi(spi, devid)`
- **Partitions:**
  - `/fs/mtd_params` (128 KB) — parameters
  - `/fs/mtd_caldata` (64 KB) — calibration
  - `/fs/mtd_waypoints` (512 KB) — missions/dataman

## Known Issues & Fixes Applied

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| MCU hangs at boot | MOSCXTBY without MOSCXTS assertion | Timeout patch in sam_clockconfig.c |
| UART garbage | PAR=0 is EVEN parity on SAMV7 | Set PAR=4 (NO parity) = 0x800 in US_MR |
| GPS 0 bytes at 38400 | Module configured at 115200 | Changed UART2 baud to 115200 |
| RC 0 bytes on ttyS3 | Wrong ttyS number | RC is on ttyS2 (UART1/PA5) |
| GPS 0 bytes on ttyS4 | Wrong ttyS number | GPS is on ttyS3 (UART2/PD25) |
| IMU1 SPI0 fails | DF40C connector MISO pin bad contact | Use IMU2 on SPI1 via J11 |
| BMP388 not found | Wrong I2C address (0x76 vs 0x77) | SDO=VCC → address 0x77 (119 decimal) |
| QSPI JEDEC mismatch | Code expected W25/S25FL, actual is SST26 | Switch to CONFIG_MTD_SST26 driver |
| sercon spam | cdcacm_autostart retrying | Disabled CONFIG_DRIVERS_CDCACM_AUTOSTART |
| MPLAB holds reset | IPE "Hold in Reset" setting | Disconnect NRST wire, power cycle |
| PICkit can't connect | Firmware hangs clock → SWD locked | Hardware erase (PB12→3.3V) |

## Hardware Notes
- ERASE pin (PB12): Short to 3.3V + power cycle = mass erase (recovers locked chip)
- TST (pin 85) and JTAGSEL (pin 104): Internal pull-downs, floating OK
- NRST: 10K pull-up (R3), 0.1uF cap (C23), 1K series (R57)
- VDDOUT: Must read 1.2V (internal regulator output)
- VDDPLL: Fed from VDDOUT via ferrite bead FB5
- XIN (PB9, pin 142): 12 MHz from DSC6011 via 0R resistor R1
- XOUT (PB8, pin 141): Left floating (bypass mode)
- Board power: USB-C (J16) provides 5V, MIC37303 regulates to 3.3V

## Build Commands

```bash
# Build
cd ~/Drone_Rathi/PX4-Autopilot-Private_Rathi
make microchip_samv71-xult-clickboards_default

# Generate HEX and copy to Windows
arm-none-eabi-objcopy -O ihex build/microchip_samv71-xult-clickboards_default/microchip_samv71-xult-clickboards_default.elf /mnt/c/Users/I73780/microchip_samv71-xult-clickboards_default.hex
cp build/microchip_samv71-xult-clickboards_default/microchip_samv71-xult-clickboards_default.elf /mnt/c/Users/I73780/

# Or use the script:
bash build_flash.sh
```

## Verified Working (2026-06-19)
- [x] Debug UART console (115200 8N1)
- [x] USB MAVLink — QGC connected
- [x] Telemetry radio (SiK, ttyS1)
- [x] IMU (ICM-45686 on SPI1, 800Hz, 0 errors)
- [x] Barometer (BMP388, I2C @0x77)
- [x] Magnetometer (BMM150, I2C @0x10)
- [x] RC Input (SBUS, TX16S + R81 V2, ttyS2)
- [x] GPS (u-blox NEO-M9N, 115200, ttyS3)
- [x] SD Card (FAT32, mount + logging)
- [x] QSPI Flash (SST26VF032B, param storage)
- [x] DShot/PWM (8 channels initialized)
- [x] Safety button (PE4)
- [ ] Motor spin with ESC (not yet tested)
- [ ] First flight
