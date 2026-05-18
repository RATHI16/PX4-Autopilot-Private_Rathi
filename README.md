# PX4 Autopilot — PIC32CZ CA70 Port

This branch contains PX4 Autopilot ported to the **Microchip PIC32CZ CA70 Curiosity** development board, with a companion **SAMD21 IO co-processor** firmware port.

## Port Status

### PIC32CZ CA70 — Main FMU

| Component | Status | Notes |
|-----------|--------|-------|
| PX4 Boot | ✅ Working | |
| NSH Shell | ✅ Working | UART1 / PKOB4 USB → `/dev/ttyACM0` |
| QSPI Flash | ✅ Working | SST26VF032B 4MB, 3 partitions |
| Parameter Storage | ✅ Working | `/fs/mtd_params` |
| Dataman | ✅ Working | `/fs/mtd_waypoints` (QSPI, SD fallback) |
| USB CDC-ACM | ✅ Working | TARGET USB → `/dev/ttyACM1` |
| MAVLink | ✅ Working | USB CDC-ACM, MAVLink v2 |
| PWM Output | ✅ Working | 4 channels, 400 Hz |
| EKF2 | ✅ Working | HITL verified |
| HITL Simulation | ✅ Working | jMAVSim via `/dev/ttyACM1` at 57600 |
| SPI Bus | ✅ Working | ICM-45686 on SPI0 |
| I2C Bus | ✅ Working | TWIHS0 (PA3=SDA, PA4=SCL) |
| ICM-45686 IMU | ✅ Working | SPI bus 1, CS=PD25, DRDY=PD28 |
| BMM150 Magnetometer | ✅ Working | I2C bus 1, addr 0x10 |
| BMP388 Barometer | ✅ Working | I2C bus 1, addr 0x76 (SDO=GND) |
| Board Defaults | ✅ Complete | RC, EKF2, PWM, PIDs, flight modes, arming |
| Hardware Watchdog | ✅ Enabled | |
| RC Receiver (SBUS) | 🔄 Params set | TX16S FrSky — hardware not yet connected |
| Real Flight Test | ⬜ Pending | Sensors verified, awaiting RC + ESC wiring |
| DShot | ⬜ Not started | |
| CAN/UAVCAN | ⬜ Not started | |

### SAMD21 — IO Co-processor

| Component | Status | Notes |
|-----------|--------|-------|
| Build | ✅ Clean | 40.7 KB flash (15.8%), 3.2 KB SRAM (9.9%) |
| NuttX BSP | ✅ Complete | SERCOM, DMAC, TC4+TC5 HRT |
| samd2l2 Platform Layer | ✅ Complete | ADC, HRT, IO pins, board reset, watchdog, version |
| SERCOM5 UART (FMU link) | ✅ Complete | 1.5 Mbps, PB16=TX, PB17=RX, DMAC |
| px4iofirmware | ✅ Integrated | SAMD serial/ADC selection via CMake |
| Pin assignments | ⚠️ Placeholders | Verify against hardware schematic before flash |
| TCC PWM output | 🔄 Stub | 8-ch TCC0/TCC1/TCC2 — implementation pending |
| sam_serial_dma_poll | 🔄 Stub | DMAC BTCNT read for short-packet detection |
| ADC channel mapping | ⚠️ Assumed | AIN0=VSERVO (PA2), AIN1=RSSI (PA3) — verify |
| Hardware testing | ⬜ Pending | Build verified; no hardware connected yet |

## Hardware Setup

- **Board:** PIC32CZ CA70 Curiosity (144-pin)
- **QSPI Flash:** SST26VF032B 4MB
- **Console:** J700 PKOB4 USB → `/dev/ttyACM0` (115200 baud)
- **MAVLink:** J200 TARGET USB → `/dev/ttyACM1`
- **Sensors** (Click boards on external base board via EXT1 header):
  - ICM-45686 — SPI, wired directly: PD20/21/22/25/28
  - BMM150 — I2C (solder-jumpered from SPI), TWIHS0
  - BMP388 — I2C (solder-jumpered from SPI), TWIHS0

## Sensor Init (`rc.board_sensors`)

```sh
board_adc start
icm45686 -s -b 1 start    # SPI bus 1, CS=PD25, DRDY=PD28
bmm150 -X -b 1 start      # I2C bus 1 (TWIHS0), addr 0x10
bmp388 -X -b 1 start      # I2C bus 1 (TWIHS0), addr 0x76
```

## HITL Setup

```sh
Tools/simulation/jmavsim/jmavsim_run.sh -d /dev/ttyACM1 -b 57600 -q
```

Requires Java 11 (not Java 21). Rebuild the jar after any Java version switch.

## Build

```sh
# PIC32CZ CA70 main FMU
make microchip_pic32czca70-curiosity_default

# SAMD21 IO co-processor
make microchip_samd21-io_default
```

### PIC32CZ CA70 Build Stats

| Region | Used | Total | Usage |
|--------|------|-------|-------|
| Flash | 1,352,048 B | 2 MB | 64.47% |
| SRAM | 53,452 B | 448 KB | 11.65% |
| nocache | 5 KB | 64 KB | 7.81% |

## Repositories

| Repo | Branch |
|------|--------|
| [Vigneshjr1/px4_pic32czca70](https://github.com/Vigneshjr1/px4_pic32czca70) | `pic32cz-ca70-port` |
| [Vigneshjr1/NuttX](https://github.com/Vigneshjr1/NuttX) | `pic32cz-ca70-port` |

```sh
git clone --recursive git@github.com:Vigneshjr1/px4_pic32czca70.git
```

## Next Steps (Real Flight)

1. Connect RC receiver (SBUS) to `/dev/ttyS3`, calibrate in QGC
2. Wire 4 ESCs to PWM outputs: CH0=PB0, CH1=PA2, CH2=PC19, CH3=PC13
3. Insert SD card for flight logging
4. Full sensor calibration in QGC (gyro → accel → compass → level)
5. Verify motor directions in QGC → Vehicle Setup → Motors
6. First flight in Stabilized mode (no GPS, barometer height only)

## Documentation

| Document | Description |
|----------|-------------|
| [QUICKSTART.md](QUICKSTART.md) | Clone, build, flash and run |
| [boards/microchip/samd21-io/PORTING_NOTES.md](boards/microchip/samd21-io/PORTING_NOTES.md) | SAMD21 IO port — every file, error, and fix |
| [docs/pic32czca70_px4_port_status.md](docs/pic32czca70_px4_port_status.md) | Detailed port status and notes |

---

*Branch: pic32cz-ca70-port*
