# PX4 Parameter Configuration Guide
## SAMV71-XULT Quadrotor — Correct Settings Reference

**Board:** `microchip/samv71-xult-clickboards`
**Vehicle:** Quadrotor X-frame (4 motors)
**PX4 Version:** 1.17.0 alpha
**Branch:** `samv7-custom`
**GPS:** Readytosky u-blox NEO-M8N
**IMU:** ICM-45686 (SPI)
**Magnetometer:** BMM150 (I2C)
**Barometer:** BMP388 (I2C)
**RC Protocol:** SBUS (UART4)

---

## How to Apply Parameters

### Method 1 — NSH Console (Recommended)

```sh
param set <NAME> <VALUE>
param save
reboot
```

### Method 2 — QGroundControl

Navigate to: **Vehicle Setup → Parameters** → search by name → change value.

### Method 3 — Load Full File

Upload a `.params` file via QGC:
**Vehicle Setup → Parameters → Tools → Load from file**

---

## 1. System Sensor Declaration

These tell PX4 which sensors are physically present. **All must match actual hardware.**

| Parameter | Correct Value | Was | Reason |
|---|---|---|---|
| `SYS_HAS_BARO` | `1` | `0` | BMP388 barometer IS present |
| `SYS_HAS_MAG` | `1` | `0` | BMM150 magnetometer IS present |
| `SYS_HAS_GPS` | `1` | `1` | NEO-M8N GPS present ✓ |
| `SYS_HAS_NUM_ASPD` | `0` | `0` | No airspeed sensor ✓ |
| `SYS_HAS_NUM_DIST` | `0` | `0` | No rangefinder ✓ |
| `SYS_HAS_NUM_OF` | `0` | `0` | No optical flow ✓ |
| `SYS_AUTOSTART` | `4001` | `4001` | Generic quadrotor X ✓ |
| `SYS_HITL` | `0` | `0` | Real flight (not simulation) ✓ |

```sh
param set SYS_HAS_BARO  1
param set SYS_HAS_MAG   1
```

---

## 2. Battery / ADC Parameters

### Hardware Reference

| Signal | SAMV71 Pin | AFEC0 Channel |
|---|---|---|
| Battery Voltage | PD30 | CH0 |
| Battery Current | PA18 | CH7 |
| Reference Voltage | — | 3.3 V (VDDANA) |

### Correct Battery Parameters

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `BAT1_SOURCE` | `0` | `0` | 0 = ADC (power module) ✓ |
| `BAT1_V_CHANNEL` | `0` | `-1` | AFEC0 CH0 = PD30 (voltage) |
| `BAT1_I_CHANNEL` | `7` | `-1` | AFEC0 CH7 = PA18 (current) |
| `BAT1_N_CELLS` | `3` | `0` | **Set to 3 (3S) or 4 (4S)** |
| `BAT1_V_DIV` | `4.03` | `-1` | Voltage divider ratio — **adjust to your resistors** |
| `BAT1_A_PER_V` | `27.27` | `-1` | Current sensor scaling — **adjust to your sensor** |
| `BAT1_V_CHARGED` | `4.20` | `4.05` | Standard LiPo full charge per cell |
| `BAT1_V_EMPTY` | `3.50` | `3.60` | Safe LiPo cutoff per cell |
| `BAT1_CAPACITY` | `2200` | `-1` | Your battery mAh (adjust to actual) |
| `BAT1_R_INTERNAL` | `-1` | `-1` | Auto internal resistance ✓ |
| `BAT1_I_OVERWRITE` | `0` | `0` | Use ADC for current ✓ |
| `BAT_LOW_THR` | `0.15` | `0.15` | 15% low battery warning ✓ |
| `BAT_CRIT_THR` | `0.07` | `0.07` | 7% critical threshold ✓ |
| `BAT_EMERGEN_THR` | `0.05` | `0.05` | 5% emergency threshold ✓ |
| `BAT_V_OFFS_CURR` | `0.0` | `0.0` | Current sensor zero offset ✓ |

### BAT1_V_DIV — How to Calculate

```
BAT1_V_DIV = (R1 + R2) / R2

Example with 10kΩ / 3.3kΩ divider:
  BAT1_V_DIV = (10000 + 3300) / 3300 = 4.03

Example with 15kΩ / 3.3kΩ divider (4S LiPo):
  BAT1_V_DIV = (15000 + 3300) / 3300 = 5.55
```

### BAT1_A_PER_V — Common Sensor Values

| Sensor | BAT1_A_PER_V |
|---|---|
| AttoPilot 45A | 13.64 |
| AttoPilot 90A | 27.27 |
| Mauch PL-200 | 67.02 |
| Generic 30A module | 36.36 |

```sh
param set BAT1_V_CHANNEL  0
param set BAT1_I_CHANNEL  7
param set BAT1_N_CELLS    3
param set BAT1_V_DIV      4.03
param set BAT1_A_PER_V    27.27
param set BAT1_V_CHARGED  4.20
param set BAT1_V_EMPTY    3.50
param set BAT1_CAPACITY   2200
```

---

## 3. GPS Parameters

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `GPS_1_CONFIG` | `201` | `201` | GPS1 serial port mapping ✓ |
| `GPS_1_PROTOCOL` | `1` | `1` | 1 = UBX (u-blox binary) ✓ |
| `GPS_1_GNSS` | `0` | `0` | 0 = auto constellation ✓ |
| `GPS_2_CONFIG` | `0` | `0` | No second GPS ✓ |
| `GPS_SAT_INFO` | `1` | `0` | Enable satellite info topic |
| `GPS_UBX_DYNMODEL` | `6` | `7` | 6 = Airborne <2G (multirotor) |
| `GPS_UBX_BAUD2` | `230400` | `230400` | Secondary baud ✓ |
| `GPS_UBX_MODE` | `0` | `0` | Standard single GPS ✓ |
| `GPS_UBX_CFG_INTF` | `0` | `0` | Auto interface config ✓ |
| `GPS_CFG_WIPE` | `0` | `0` | Don't wipe GPS config ✓ |
| `GPS_DUMP_COMM` | `0` | `0` | No debug dump ✓ |
| `GPS_YAW_OFFSET` | `0.0` | `0.0` | GPS mounted forward ✓ |
| `SER_GPS1_BAUD` | `0` | `57600` | 0 = auto-negotiate baud |

### GPS UBX Dynamic Model Reference

| Value | Model | Use For |
|---|---|---|
| 2 | Stationary | Fixed ground station |
| 3 | Pedestrian | Walking speeds |
| 4 | Automotive | Ground vehicles |
| 6 | Airborne <2G | **Multirotor (recommended)** |
| 7 | Airborne <4G | High-speed aircraft |

```sh
param set GPS_SAT_INFO     1
param set GPS_UBX_DYNMODEL 6
param set SER_GPS1_BAUD    0
```

---

## 4. EKF2 Parameters

### Height Reference & Sensor Fusion

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `EKF2_EN` | `1` | `1` | EKF2 enabled ✓ |
| `EKF2_HGT_REF` | `1` | `1` | 1 = Barometer as height ref ✓ |
| `EKF2_BARO_CTRL` | `1` | `1` | Barometer fusion enabled ✓ |
| `EKF2_BARO_DELAY` | `0` | `0` | BMP388 latency ✓ |
| `EKF2_BARO_NOISE` | `3.5` | `3.5` | Baro noise level ✓ |

### GPS Fusion

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `EKF2_GPS_CTRL` | `7` | `7` | Bits 0+1+2 = pos+vel+hgt ✓ |
| `EKF2_GPS_DELAY` | `110` | `110` | NEO-M8N latency (ms) ✓ |
| `EKF2_GPS_P_NOISE` | `0.5` | `0.5` | GPS position noise ✓ |
| `EKF2_GPS_V_NOISE` | `0.3` | `0.3` | GPS velocity noise ✓ |
| `EKF2_GPS_CHECK` | `2047` | `2047` | All GPS checks enabled ✓ |
| `EKF2_GPS_MODE` | `0` | `0` | Standard GPS mode ✓ |
| `EKF2_GPS_POS_X/Y/Z` | `0.0` | `0.0` | GPS at IMU location ✓ |

### Magnetometer Fusion

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `EKF2_MAG_TYPE` | `0` | `0` | 0 = Auto (use when good) ✓ |
| `EKF2_MAG_CHECK` | `1` | `1` | Mag consistency check ✓ |
| `EKF2_MAG_DELAY` | `0` | `0` | BMM150 latency ✓ |
| `EKF2_DECL_TYPE` | `3` | `3` | 3 = Auto magnetic declination ✓ |
| `EKF2_MAG_NOISE` | `0.05` | `0.05` | Mag noise ✓ |

### Optical Flow & Rangefinder — DISABLE (no sensors)

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `EKF2_OF_CTRL` | `0` | `1` | No optical flow sensor — disable |
| `EKF2_RNG_CTRL` | `0` | `1` | No rangefinder — disable |

### GPS Quality Requirements (Pre-arm Checks)

| Parameter | Correct Value | Notes |
|---|---|---|
| `EKF2_REQ_NSATS` | `6` | Min 6 satellites |
| `EKF2_REQ_EPH` | `3.0` | Max 3m horizontal accuracy |
| `EKF2_REQ_EPV` | `5.0` | Max 5m vertical accuracy |
| `EKF2_REQ_HDRIFT` | `0.1` | Max GPS horizontal drift |
| `EKF2_REQ_PDOP` | `2.5` | Max PDOP |
| `EKF2_REQ_FIX` | `3` | Minimum 3D fix required |

```sh
param set EKF2_OF_CTRL  0
param set EKF2_RNG_CTRL 0
```

---

## 5. RC Parameters

### Serial Port & Protocol

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `RC_INPUT_PROTO` | `2` | `2` | 2 = SBUS ✓ |
| `RC_PORT_CONFIG` | `300` | `300` | RC port mapping ✓ |
| `RC_CHAN_CNT` | `18` | `18` | 18 SBUS channels ✓ |

### Stick Channel Mapping (Mode 2)

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `RC_MAP_ROLL` | `1` | `1` | CH1 → Roll (aileron) ✓ |
| `RC_MAP_PITCH` | `2` | `2` | CH2 → Pitch (elevator) ✓ |
| `RC_MAP_THROTTLE` | `3` | `3` | CH3 → Throttle ✓ |
| `RC_MAP_YAW` | `4` | `4` | CH4 → Yaw (rudder) ✓ |
| `RC_MAP_FLTMODE` | `6` | `6` | CH6 → Flight mode switch ✓ |
| `RC_MAP_KILL_SW` | `11` | `11` | CH11 → Kill switch ✓ |
| `RC_MAP_ARM_SW` | `10` | `10` | CH10 → Arm switch ✓ |
| `RC_MAP_RETURN_SW` | `7` | `7` | CH7 → RTL switch ✓ |

### Channel Reversals

| Parameter | Correct Value | Notes |
|---|---|---|
| `RC1_REV` | `1.0` | Roll — normal direction ✓ |
| `RC2_REV` | `-1.0` | Pitch — reversed ✓ (elevator convention) |
| `RC3_REV` | `1.0` | Throttle — normal ✓ |
| `RC4_REV` | `-1.0` | Yaw — reversed (check TX orientation) |

### RC Channel Calibration Reference

| Channel | Min | Trim | Max | Function |
|---|---|---|---|---|
| RC1 (Roll) | 1000 | 1500 | 2000 | Normal |
| RC2 (Pitch) | 1000 | 1500 | 2000 | Reversed |
| RC3 (Throttle) | 1000 | 1500 | 2000 | Normal |
| RC4 (Yaw) | 982 | **1500** | 1981 | Reversed — **recalibrate trim to 1500** |

> RC4_TRIM is currently 1481. After zeroing transmitter trim and recalibrating,
> this should be 1500. Run `commander calibrate rc` after TX trim is zeroed.

### RC Safety Thresholds

| Parameter | Correct Value | Notes |
|---|---|---|
| `RC_ARMSWITCH_TH` | `0.75` | >75% on CH10 = armed ✓ |
| `RC_KILLSWITCH_TH` | `0.75` | >75% on CH11 = kill ✓ |
| `RC_RETURN_TH` | `0.75` | >75% on CH7 = RTL ✓ |
| `RC_FAILS_THR` | `0` | 0 = use RC loss timeout ✓ |
| `COM_RC_LOSS_T` | `0.5` | 500ms before RC loss action ✓ |

### Flight Mode Assignments (CH6)

| Position | Parameter | Mode | Mode Number |
|---|---|---|---|
| Position 1 | `COM_FLTMODE1` | Position | `8` |
| Position 2 | `COM_FLTMODE2` | — (unused) | `-1` |
| Position 3 | `COM_FLTMODE3` | — (unused) | `-1` |
| Position 4 | `COM_FLTMODE4` | Takeoff | `1` |
| Position 5 | `COM_FLTMODE5` | — (unused) | `-1` |
| Position 6 | `COM_FLTMODE6` | Return (RTL) | `2` |

> Suggested addition: Set COM_FLTMODE2=6 (Stabilized) or COM_FLTMODE3=17
> (Altitude) for manual/assisted modes as backup.

---

## 6. Motor / PWM Parameters

### Output Configuration

| Parameter | Correct Value | Notes |
|---|---|---|
| `PWM_MAIN_FUNC1` | `101` | Output 1 = Motor 1 ✓ |
| `PWM_MAIN_FUNC2` | `102` | Output 2 = Motor 2 ✓ |
| `PWM_MAIN_FUNC3` | `103` | Output 3 = Motor 3 ✓ |
| `PWM_MAIN_FUNC4` | `104` | Output 4 = Motor 4 ✓ |
| `PWM_MAIN_TIM0` | `400` | 400 Hz PWM rate (SAMV71 PWMC0) ✓ |

### PWM Limits

| Parameter | Correct Value | Notes |
|---|---|---|
| `PWM_MAIN_MIN1-4` | `1100` | Motor arm point (µs) ✓ |
| `PWM_MAIN_MAX1-4` | `1900` | Motor full throttle (µs) ✓ |
| `PWM_MAIN_DIS1-4` | `1000` | Disarmed signal (µs) ✓ |
| `PWM_MAIN_FAIL1-4` | `-1` | No failsafe override ✓ |
| `PWM_MAIN_REV` | `0` | No channel reversal ✓ |

---

## 7. Control Allocator (Motor Geometry)

X-frame quadrotor, motors pointing downward (thrust upward).

| Motor | Pin | Position (X, Y) | Spin | KM |
|---|---|---|---|---|
| Motor 1 (ROTOR0) | PB0 | Front-Right (+1, +1) | CW | +0.05 |
| Motor 2 (ROTOR1) | PA2 | Rear-Left (-1, -1) | CW | +0.05 |
| Motor 3 (ROTOR2) | PC19 | Front-Left (+1, -1) | CCW | -0.05 |
| Motor 4 (ROTOR3) | PC13 | Rear-Right (-1, +1) | CCW | -0.05 |

| Parameter | Correct Value | Notes |
|---|---|---|
| `CA_AIRFRAME` | `0` | Multirotor ✓ |
| `CA_METHOD` | `2` | Geometric allocation ✓ |
| `CA_ROTOR_COUNT` | `4` | 4 motors ✓ |
| `CA_ROTOR0/1/2/3_AZ` | `-1.0` | All rotors thrust upward ✓ |

> All CA_ROTOR* positions and KM signs are correctly configured — no changes needed.

---

## 8. Commander (Arming & Safety)

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `COM_ARMABLE` | `1` | `1` | Arming enabled ✓ |
| `COM_ARM_WO_GPS` | `1` | `1` | Can arm without GPS (dev mode) |
| `COM_ARM_MAG_ANG` | `60` | `60` | Max 60° mag inconsistency ✓ |
| `COM_ARM_IMU_ACC` | `0.7` | `0.7` | Accel check threshold ✓ |
| `COM_ARM_IMU_GYR` | `0.25` | `0.25` | Gyro check threshold ✓ |
| `COM_ARM_HFLT_CHK` | `1` | `1` | Hardfault check ✓ |
| `COM_ARM_SDCARD` | `0` | `0` | No SD card required ✓ |
| `COM_LOW_BAT_ACT` | `3` | `3` | 3 = Land on low battery ✓ |
| `COM_DISARM_LAND` | `10` | `10` | Auto-disarm 10s after land ✓ |
| `COM_DISARM_PRFLT` | `10` | `10` | Auto-disarm 10s if not flying ✓ |
| `COM_RC_OVERRIDE` | `1` | `1` | RC can override auto mode ✓ |
| `COM_MOT_TEST_EN` | `1` | `1` | Motor test enabled (dev) |
| `COM_SPOOLUP_TIME` | `1.0` | `1.0` | 1s motor spool-up time ✓ |

---

## 9. MAVLink

| Parameter | Correct Value | Was | Notes |
|---|---|---|---|
| `MAV_0_CONFIG` | `101` | `101` | MAVLink on USB/TEL1 ✓ |
| `MAV_0_MODE` | `0` | `0` | Normal mode ✓ |
| `MAV_0_RATE` | `100000` | `100000` | 100 kbps (USB rate) ✓ |
| `MAV_0_FLOW_CTRL` | `2` | `2` | Hardware flow control ✓ |
| `MAV_TYPE` | `2` | `2` | 2 = Quadrotor ✓ |
| `MAV_SYS_ID` | `1` | `1` | Vehicle ID ✓ |
| `MAV_COMP_ID` | `1` | `1` | Component ID ✓ |
| `MAV_PROTO_VER` | `2` | `2` | MAVLink v2 ✓ |
| `SER_TEL1_BAUD` | `57600` | `57600` | TEL1 baud (USB ignores this) ✓ |

---

## 10. Sensor Calibration Status

### IMU (ICM-45686) — Done ✓

| Parameter | Status |
|---|---|
| `CAL_ACC0_ID = 3407882` | Valid calibration ID |
| `CAL_GYRO0_ID = 3407882` | Valid calibration ID |
| Offsets (XOFF/YOFF/ZOFF) | Non-zero — calibrated |
| Scale (XSCALE/YSCALE/ZSCALE) | Close to 1.0 — good |

### Magnetometer (BMM150) — Partially Done ⚠️

| Parameter | Value | Status |
|---|---|---|
| `CAL_MAG0_ID = 4395017` | Valid | Good |
| `CAL_MAG0_XSCALE = 0.963` | Close to 1.0 | Acceptable |
| `CAL_MAG0_YSCALE = 0.966` | Close to 1.0 | Acceptable |
| `CAL_MAG0_ZSCALE = 0.836` | **16% off** | **Redo calibration** |

> Z-axis scale of 0.836 indicates incomplete calibration coverage on the Z axis.
> Redo with careful upside-down and vertical orientations.

```sh
commander calibrate mag
# Rotate through all 6 faces: +X, -X, +Y, -Y, +Z (flat), -Z (upside-down)
```

---

## 11. Circuit Breakers

### Current State (Development Mode)

| Parameter | Current Value | Meaning |
|---|---|---|
| `CBRK_SUPPLY_CHK` | `894281` | Battery check bypassed |
| `CBRK_IO_SAFETY` | `22027` | IO safety bypassed |
| `CBRK_USB_CHK` | `197848` | USB check bypassed |
| `CBRK_BUZZER` | `782090` | Buzzer check bypassed |
| `CBRK_FLIGHTTERM` | `121212` | Flight termination bypassed |

### For Real Flight — Remove All Bypasses

```sh
param set CBRK_SUPPLY_CHK  0
param set CBRK_IO_SAFETY   0
param set CBRK_USB_CHK     0
param set CBRK_BUZZER      0
param set CBRK_FLIGHTTERM  0
param save
reboot
```

---

## 12. SDLOG (Data Logging)

| Parameter | Correct Value | Notes |
|---|---|---|
| `SDLOG_BACKEND` | `3` | 3 = ULog ✓ |
| `SDLOG_MODE` | `-1` | -1 = Armed only ✓ |
| `SDLOG_PROFILE` | `1` | Default log profile ✓ |
| `SDLOG_BOOT_BAT` | `0` | No boot battery log ✓ |

---

## 13. Complete Fix Script

Copy and paste into NSH console to apply all corrections at once:

```sh
# ── SYSTEM SENSOR FLAGS ────────────────────────────────────────────────────
param set SYS_HAS_BARO      1
param set SYS_HAS_MAG       1

# ── BATTERY ADC ────────────────────────────────────────────────────────────
param set BAT1_V_CHANNEL    0        # PD30 = AFEC0 CH0
param set BAT1_I_CHANNEL    7        # PA18 = AFEC0 CH7
param set BAT1_N_CELLS      3        # 3 = 3S LiPo (change to 4 for 4S)
param set BAT1_V_DIV        4.03     # adjust to your resistor divider ratio
param set BAT1_A_PER_V      27.27    # adjust to your current sensor spec
param set BAT1_V_CHARGED    4.20     # standard LiPo full charge
param set BAT1_V_EMPTY      3.50     # safe LiPo cutoff
param set BAT1_CAPACITY     2200     # mAh - adjust to your battery

# ── GPS ────────────────────────────────────────────────────────────────────
param set GPS_SAT_INFO      1        # enable satellite info
param set GPS_UBX_DYNMODEL  6        # airborne <2G for multirotor
param set SER_GPS1_BAUD     0        # auto-negotiate baud rate

# ── EKF2 FUSION ────────────────────────────────────────────────────────────
param set EKF2_OF_CTRL      0        # no optical flow sensor
param set EKF2_RNG_CTRL     0        # no rangefinder sensor

# ── SAVE AND REBOOT ────────────────────────────────────────────────────────
param save
reboot
```

---

## 14. Post-Reboot Checklist

After applying parameters and rebooting:

```sh
# 1. Verify sensors detected
listener sensor_combined        # should show IMU data
listener sensor_baro            # should show pressure/altitude
listener vehicle_magnetometer   # should show mag field

# 2. Verify GPS working
listener sensor_gps             # should show fix_type=3 outdoors
gps status

# 3. Verify battery reading
listener battery_status         # should show voltage_v > 0 and connected=1
listener adc_report             # raw ADC counts for CH0 and CH7

# 4. Verify RC input
listener input_rc               # move sticks and verify channels respond

# 5. Redo magnetometer calibration (Z scale issue)
commander calibrate mag

# 6. Redo RC calibration (yaw trim)
commander calibrate rc
```

---

## 15. Parameter Change Summary

### Changes Required (11 parameters)

| Parameter | Old Value | New Value | Priority |
|---|---|---|---|
| `SYS_HAS_BARO` | `0` | `1` | CRITICAL |
| `SYS_HAS_MAG` | `0` | `1` | CRITICAL |
| `BAT1_V_CHANNEL` | `-1` | `0` | CRITICAL |
| `BAT1_I_CHANNEL` | `-1` | `7` | CRITICAL |
| `BAT1_N_CELLS` | `0` | `3` or `4` | CRITICAL |
| `BAT1_V_DIV` | `-1` | measure & calculate | CRITICAL |
| `BAT1_A_PER_V` | `-1` | per sensor spec | CRITICAL |
| `BAT1_V_CHARGED` | `4.05` | `4.20` | HIGH |
| `EKF2_OF_CTRL` | `1` | `0` | HIGH |
| `EKF2_RNG_CTRL` | `1` | `0` | HIGH |
| `GPS_SAT_INFO` | `0` | `1` | MEDIUM |
| `GPS_UBX_DYNMODEL` | `7` | `6` | MEDIUM |
| `SER_GPS1_BAUD` | `57600` | `0` | MEDIUM |

### Parameters That Are Correct (No Change Needed)

All `CA_ROTOR*`, `PWM_MAIN_*`, `RC_MAP_*`, `COM_ARM_*`, `EKF2_GPS_*`,
`MAV_*`, `MPC_*`, `MC_*`, `SDLOG_*`, `CAL_ACC0_*`, `CAL_GYRO0_*`,
`GPS_1_PROTOCOL`, `GPS_1_CONFIG`, `EKF2_GPS_DELAY`, `EKF2_GPS_CTRL`,
`EKF2_HGT_REF`, `EKF2_BARO_CTRL`, `RC_INPUT_PROTO`, `SYS_AUTOSTART`,
`SYS_HAS_GPS`, `MAV_TYPE`.

---

*Document generated for PX4 branch `samv7-custom`, board `microchip/samv71-xult-clickboards`.*
*Version: 1.17.0 alpha — Git: debeeba28b*
