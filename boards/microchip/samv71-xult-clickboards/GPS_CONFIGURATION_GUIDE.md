# GPS Configuration Guide
## SAMV71-XULT + Readytosky u-blox NEO-M8N

**Board:** `microchip/samv71-xult-clickboards`
**GPS Module:** Readytosky u-blox NEO-M8N (for APM / Pixhawk)
**PX4 Branch:** `samv7-custom`

---

## 1. Module Overview

| Property         | Detail                              |
|------------------|-------------------------------------|
| GPS Chip         | u-blox NEO-M8N                      |
| Protocol         | UBX (binary) — auto-negotiated      |
| Default Baud     | 9600 baud (factory default)         |
| PX4 Target Baud  | Auto-negotiates up to 115200        |
| Built-in Compass | QMC5883L (I2C, addr 0x0D) — **not used** (BMM150 Click already present) |
| Power            | 5 V supply, 3.3 V UART logic        |
| Connector        | 6-pin Dupont / JST                  |
| UART Device      | `/dev/ttyS2` → SAMV71 UART2         |

> **Compass note:** The NEO-M8N has a built-in QMC5883L magnetometer on I2C.
> This board already uses a BMM150 (GeoMagnetic Click) as its magnetometer.
> The GPS built-in compass is **not connected and not needed**.

---

## 2. Hardware Wiring

### UART2 Pin Mapping (SAMV71)

| SAMV71 Pin | UART2 Signal | GPS Module Pin | GPS Signal |
|------------|--------------|----------------|------------|
| PD25       | URXD2 (RX)   | Pin 3          | TX (GPS output) |
| PD26       | UTXD2 (TX)   | Pin 2          | RX (GPS input)  |
| 5V rail    | Power        | Pin 1          | VCC (5V)        |
| GND        | Ground       | Pin 6          | GND             |
| —          | Not connected| Pin 4          | SDA (compass — unused) |
| —          | Not connected| Pin 5          | SCL (compass — unused) |

> **Voltage compatibility:** SAMV71 operates at 3.3 V logic. The NEO-M8N UART
> signals are 3.3 V compatible — no level shifter required.

### Serial Port Assignment Summary

| `/dev/` Device | SAMV71 Peripheral | PX4 Role | Baud Rate     |
|----------------|-------------------|----------|---------------|
| `/dev/ttyS0`   | USART1            | Console  | 115200        |
| `/dev/ttyS1`   | UART0             | TEL2     | 115200        |
| `/dev/ttyS2`   | UART2             | **GPS1** | auto (9600→115200) |
| `/dev/ttyS3`   | UART4             | RC Input | 100000, EVEN parity |
| `/dev/ttyACM0` | USB CDC/ACM       | TEL1     | —             |

---

## 3. Firmware Configuration

### 3.1 `default.px4board` — Status: Already Configured

The following lines are already present and correct:

```
CONFIG_BOARD_SERIAL_GPS1="/dev/ttyS2"
CONFIG_DRIVERS_GPS=y
```

No changes needed in this file for GPS UART operation.

### 3.2 `nuttx-config/nsh/defconfig` — UART2 Settings

Current UART2 configuration (lines 260–265):

```
CONFIG_UART2_BAUD=57600       # Initial baud — GPS driver overrides this
CONFIG_UART2_BITS=8
CONFIG_UART2_PARITY=0
CONFIG_UART2_2STOP=0
CONFIG_UART2_RXBUFSIZE=256    # Recommended: increase to 512
CONFIG_UART2_TXBUFSIZE=256    # Recommended: increase to 512
```

**Recommended change** — increase RX/TX buffers to prevent overflow at 10 Hz
GPS update rate:

```diff
-CONFIG_UART2_RXBUFSIZE=256
-CONFIG_UART2_TXBUFSIZE=256
+CONFIG_UART2_RXBUFSIZE=512
+CONFIG_UART2_TXBUFSIZE=512
```

> The initial baud of 57600 in defconfig does not matter. PX4's GPS driver
> automatically scans all baud rates (9600, 38400, 57600, 115200) and
> reconfigures the port and the module to the highest supported rate.

### 3.3 `init/rc.board_sensors` — No GPS Entry Needed

The GPS driver is started automatically by PX4's `rc.serial` script based on
`CONFIG_BOARD_SERIAL_GPS1`. No entry is required in `rc.board_sensors`.

Current file (for reference):

```sh
# ICM-45686 IMU (accel + gyro) - SPI bus 1
icm45686 -s -b 1 start

# BMM150 Magnetometer - I2C bus 1  ← this handles compass, GPS compass unused
bmm150 -X -b 1 start

# BMP388 Barometer - I2C bus 1
bmp388 -X -b 1 start
```

### 3.4 `default.px4board` — EKF2 GPS Fusion (Already Enabled)

```
CONFIG_MODULES_EKF2=y
CONFIG_EKF2_GNSS_YAW=y        # GPS heading fusion
CONFIG_EKF2_AUX_GLOBAL_POSITION=y
CONFIG_EKF2_AUXVEL=y
```

---

## 4. Runtime PX4 Parameters

Set these once via NSH console or QGroundControl. Parameters are saved to
`/fs/mtd_params` (QSPI flash).

### 4.1 GPS Parameters

```sh
param set GPS_1_PROTOCOL   1     # 1 = UBX (u-blox binary protocol)
param set GPS_SAT_INFO     1     # Enable satellite info topic
param set GPS_UBX_DYNMODEL 6     # 6 = Airborne <2G (drone use)
```

| Parameter       | Value | Description                                      |
|-----------------|-------|--------------------------------------------------|
| `GPS_1_PROTOCOL`| `1`   | Force UBX protocol — faster lock, richer data    |
| `GPS_SAT_INFO`  | `1`   | Publish `satellite_info` uORB topic              |
| `GPS_UBX_DYNMODEL` | `6` | Airborne <2G — optimised for multicopter flight |

> `GPS_UBX_DYNMODEL` options: 2=stationary, 3=pedestrian, 4=automotive,
> 6=airborne <2G, 7=airborne <4G. Use **6** for normal drone operation.

### 4.2 EKF2 GPS Fusion Parameters

```sh
param set EKF2_GPS_CTRL   15    # Enable all GPS fusion (pos + vel + hgt + yaw)
param set EKF2_GPS_DELAY  110   # Typical NEO-M8N latency in ms
param set EKF2_GPS_POS_X  0.0   # GPS antenna offset from IMU (metres)
param set EKF2_GPS_POS_Y  0.0
param set EKF2_GPS_POS_Z  0.0
```

| Parameter        | Value | Description                             |
|------------------|-------|-----------------------------------------|
| `EKF2_GPS_CTRL`  | `15`  | Fuse GPS position, velocity, height, yaw|
| `EKF2_GPS_DELAY` | `110` | NEO-M8N signal latency (ms)             |
| `EKF2_GPS_POS_X/Y/Z` | `0.0` | Lever arm offset (measure if GPS not at IMU) |

### 4.3 Save and Apply

```sh
param save
reboot
```

---

## 5. Testing Procedure

### Step 1 — Verify UART2 is Alive

```sh
# Check that /dev/ttyS2 exists
ls /dev/ttyS2
```

Expected: device node present (no "No such file or directory").

### Step 2 — Start GPS Driver Manually (if not auto-started)

```sh
# Let PX4 auto-negotiate baud rate (recommended)
gps start -d /dev/ttyS2

# Or if you know the module is already at a specific baud:
gps start -d /dev/ttyS2 -b 9600
```

> Do **not** hard-code `-b 38400` unless the module was previously configured
> to 38400. The NEO-M8N ships at 9600 baud. PX4 will auto-detect and upgrade.

### Step 3 — Check GPS Driver Status

```sh
gps status
```

**Good output:**
```
GPS driver status:
  port:              /dev/ttyS2
  baudrate:          115200
  protocol:          UBX
  satellites used:   9
  fix type:          3D Fix
  HDOP:              1.1
  VDOP:              1.8
  latitude:          xx.xxxxxx deg
  longitude:         xx.xxxxxx deg
  altitude:          xxx.x m
```

**Problem indicators:**

| Output                  | Cause                              | Fix                                  |
|-------------------------|------------------------------------|--------------------------------------|
| `protocol: NMEA`        | UBX not negotiated yet             | Wait 10 s or set `GPS_1_PROTOCOL=1`  |
| `satellites used: 0`    | No sky view / still acquiring      | Move outdoors, wait 1–2 min          |
| `fix type: No Fix`      | Acquiring satellites               | Wait outdoors with clear sky view    |
| `baudrate: 38400`       | Module at non-default baud         | Let auto-detect run, or factory reset |
| Driver won't start      | Wrong device path                  | Verify `/dev/ttyS2` exists           |

### Step 4 — Monitor GPS Data Stream

```sh
# Raw GPS sensor data (position, velocity, fix quality)
listener sensor_gps

# EKF2-fused GPS position (used by flight controller)
listener vehicle_gps_position

# Satellite-by-satellite detail (requires GPS_SAT_INFO=1)
listener satellite_info
```

### Step 5 — Verify EKF2 is Using GPS

```sh
listener estimator_status
```

Look for `gps_check_fail_flags: 0` — zero means all GPS health checks passed.

```sh
listener vehicle_local_position
```

Look for `xy_valid: 1` and `z_valid: 1` — EKF2 has a valid position estimate.

---

## 6. Expected `listener` Outputs (Healthy System)

### `listener sensor_gps`

```
sensor_gps
  timestamp: 1234567890
  fix_type: 3              ← 3 = 3D Fix (need ≥3 for flight)
  satellites_used: 9       ← need ≥6 for stable fix
  lat: 28.123456           ← latitude in degrees
  lon: 77.123456           ← longitude in degrees
  alt: 220.350             ← altitude MSL in metres
  eph: 1.2                 ← horizontal accuracy estimate (m), want <3.0
  epv: 2.1                 ← vertical accuracy estimate (m)
  hdop: 1.1                ← horizontal dilution of precision, want <2.0
  vdop: 1.8                ← vertical dilution of precision
  vel_n_m_s: 0.01          ← north velocity (m/s)
  vel_e_m_s: 0.02          ← east velocity (m/s)
  vel_d_m_s: 0.00          ← down velocity (m/s)
```

### `listener satellite_info`

```
satellite_info
  count: 12                ← satellites visible in sky
  svid[0]: 5               ← satellite vehicle ID
  elevation[0]: 45         ← elevation angle (degrees above horizon)
  azimuth[0]: 180          ← azimuth (degrees)
  snr_dbhz[0]: 38          ← signal strength, want ≥30 dBHz per satellite
  used[0]: 1               ← 1 = satellite used in fix
```

### `listener vehicle_gps_position`

```
vehicle_gps_position
  fix_type: 3
  lat: 28.123456
  lon: 77.123456
  alt: 220350              ← in mm (divide by 1000 for metres)
  satellites_used: 9
  hdop: 1.1
  eph: 1.2
```

---

## 7. Fix Type Reference

| `fix_type` Value | Meaning               | Usable for Flight? |
|------------------|-----------------------|--------------------|
| 0                | No fix                | No                 |
| 1                | Dead reckoning only   | No                 |
| 2                | 2D fix (no altitude)  | No                 |
| 3                | 3D fix                | Yes — minimum      |
| 4                | GNSS + dead reckoning | Yes                |
| 5                | Time-only fix         | No                 |

---

## 8. GPS Quality Thresholds (PX4 Pre-arm Checks)

PX4 will block arming if GPS quality is below these defaults:

| Check                  | Parameter           | Default Threshold |
|------------------------|---------------------|-------------------|
| Minimum satellites     | `EKF2_REQ_NSATS`    | 6                 |
| Max HDOP               | `EKF2_REQ_HDOP`     | 2.5               |
| Max PDOP               | `EKF2_REQ_PDOP`     | 5.0               |
| Max EPH (horiz acc)    | `EKF2_REQ_EPH`      | 3.0 m             |
| Max EPV (vert acc)     | `EKF2_REQ_EPV`      | 5.0 m             |
| Min fix type           | (internal)          | 3D Fix            |

---

## 9. Troubleshooting

### GPS driver starts but no data

- Verify TX/RX are not swapped (GPS TX → SAMV71 RX = PD25)
- Check 5 V supply to GPS module
- Try `gps stop` then `gps start -d /dev/ttyS2` to force re-init

### Stuck at 2D fix / few satellites

- **Must be outdoors** with clear sky view
- NEO-M8N cold start: 26–30 seconds typical, up to 2 minutes after long storage
- Warm start (last position known): under 5 seconds

### `gps status` shows wrong baud rate

```sh
gps stop
gps start -d /dev/ttyS2     # re-run auto-negotiation
```

### EKF2 not using GPS (`xy_valid: 0`)

```sh
listener estimator_status    # check gps_check_fail_flags
```

Common flags:
- Bit 0: `fix` — not enough satellites or no 3D fix
- Bit 1: `nsats` — fewer than `EKF2_REQ_NSATS` satellites
- Bit 2: `hdop` — HDOP exceeds `EKF2_REQ_HDOP`
- Bit 4: `hacc` — horizontal accuracy > `EKF2_REQ_EPH`

### Factory reset NEO-M8N baud to 9600

If the module was previously configured to a non-standard baud rate:

```sh
# In NSH — send UBX CFG-PRT command to reset to 9600
# Simplest: power-cycle the GPS module (NEO-M8N reverts to 9600 on cold boot
# only if not saved to flash — use u-center on Windows to factory reset if needed)
```

---

## 10. Configuration Checklist

### Firmware (compile-time)

- [x] `CONFIG_BOARD_SERIAL_GPS1="/dev/ttyS2"` — in `default.px4board`
- [x] `CONFIG_DRIVERS_GPS=y` — in `default.px4board`
- [x] `CONFIG_SAMV7_UART2=y` — in `defconfig`
- [x] `CONFIG_EKF2_GNSS_YAW=y` — in `default.px4board`
- [ ] `CONFIG_UART2_RXBUFSIZE=512` — increase from 256 in `defconfig`
- [ ] `CONFIG_UART2_TXBUFSIZE=512` — increase from 256 in `defconfig`

### Hardware

- [ ] GPS TX → PD25 (UART2 RX) connected
- [ ] GPS RX → PD26 (UART2 TX) connected
- [ ] GPS VCC → 5 V rail connected
- [ ] GPS GND → GND connected
- [ ] GPS SDA / SCL — left unconnected (compass not used)

### Runtime Parameters

- [ ] `GPS_1_PROTOCOL = 1` (UBX)
- [ ] `GPS_SAT_INFO = 1` (enable satellite topic)
- [ ] `GPS_UBX_DYNMODEL = 6` (airborne <2G)
- [ ] `EKF2_GPS_CTRL = 15` (full GPS fusion)
- [ ] `EKF2_GPS_DELAY = 110` (NEO-M8N latency)
- [ ] `param save` + `reboot` after setting params

---

*Document generated for PX4 branch `samv7-custom`, board `microchip/samv71-xult-clickboards`.*
