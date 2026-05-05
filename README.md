# PX4-Autopilot-Private (SAMV71 Branch)

This branch contains PX4 Autopilot ported to Microchip SAMV71-XULT development board.

## SAMV71 Port Status

| Component | Status |
|-----------|--------|
| PX4 Boot | ✅ Working |
| NSH Shell | ✅ Working |
| SD Card Logging | ✅ Working |
| ICM20689 IMU | ✅ Working |
| SPI Bus | ✅ Working |
| I2C Bus | ✅ Working |
| PWM Output | ✅ Working (3 channels) |
| USB CDC-ACM | ✅ Working |
| MAVLink | ✅ Working |
| VBUS Detection | 🔄 Stubbed (always present) |
| BMP388 Barometer |✅ Working  |
| AK09915 Magnetometer | ✅ Working  |
| DShot | In progress|
| CAN/UAVCAN | ⬜ Not Started |

## Documentation

| Document | Description |
|----------|-------------|
| [Team Setup Guide](docs/SAMV7_TEAM_SETUP_GUIDE.md) | How to clone and build |
| [Execution Plan](docs/SAMV71_FMUV6_PARITY_EXECUTION_PLAN.md) | Acceptance criteria & test checklists |
| [Development Roadmap](docs/SAMV71_FMUV6_PARITY_ROADMAP.md) | 5-phase FMUv6 parity roadmap |
| [Gap Analysis](docs/SAMV71_PORT_GAP_ANALYSIS.md) | SAMV71 vs STM32 comparison |
| [Changelog](docs/CHANGELOG_2024-12-12.md) | Recent changes |

## Quick Start

```bash
# Clone with submodules
git clone --recursive -b samv7-custom git@github.com:bhanuprakashjh/PX4-Autopilot-Private.git
cd PX4-Autopilot-Private

# Build
make microchip_samv71-xult-clickboards_default

# Flash (with EDBG debugger)
openocd -f interface/cmsis-dap.cfg -c "adapter speed 1000" -f target/atsamv.cfg \
    -c "program build/microchip_samv71-xult-clickboards_default/microchip_samv71-xult-clickboards_default.elf verify reset exit"
```

## Build Stats

| Region | Used | Total | Usage |
|--------|------|-------|-------|
| Flash | 1,340,340 B | 2 MB | 63.91% |
| SRAM | 52,588 B | 320 KB | 16.05% |
| nocache | 5 KB | 64 KB | 7.81% |

## NuttX Submodule

This branch uses a forked NuttX with SAMV7 HSMCI/DMA fixes:
- **Fork:** [bhanuprakashjh/NuttX](https://github.com/bhanuprakashjh/NuttX)
- **Branch:** `px4_firmware_nuttx-samv7`

---

*Branch: samv7-custom*
