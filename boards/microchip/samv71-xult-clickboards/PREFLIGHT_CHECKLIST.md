# Pre-Flight Checklist
## SAMV71-XULT Quadrotor — First Flight

---

**Date:** ___________________
**Location:** ___________________
**Pilot:** ___________________
**Battery Used:** ___________________
**Flight Number:** ___________________

---

> **RULE 1 — If in doubt, do not fly.**
> **RULE 2 — Always remove propellers during ground testing.**
> **RULE 3 — Never arm near people or animals.**

---

## SECTION 1 — Pre-Flight Preparation

### 1.1 Site Check

| # | Check | Done |
|---|---|---|
| 1 | Flying area is open, clear of obstacles | ☐ |
| 2 | No people or animals within 30 m radius | ☐ |
| 3 | Wind speed acceptable (< 5 m/s for first flight) | ☐ |
| 4 | Weather is clear — no rain, fog, or storm | ☐ |
| 5 | Ground is flat and firm for takeoff | ☐ |
| 6 | GPS sky view is clear (no trees, buildings overhead) | ☐ |

---

### 1.2 Battery Preparation

| # | Check | Done |
|---|---|---|
| 1 | Battery fully charged (4.20V per cell) | ☐ |
| 2 | Battery has no puffing, swelling, or damage | ☐ |
| 3 | Battery connectors clean and undamaged | ☐ |
| 4 | Battery voltage confirmed with multimeter | ☐ |
| 5 | 3S = 12.6V fully charged / 4S = 16.8V fully charged | ☐ |

> **Never fly with a battery below 3.7V per cell at rest.**

---

## SECTION 2 — Physical Inspection (Power OFF)

### 2.1 Frame

| # | Check | Done |
|---|---|---|
| 1 | All frame arms are tight — no cracks or loose joints | ☐ |
| 2 | Frame center plate screws all tight | ☐ |
| 3 | No visible damage from previous flight or transport | ☐ |
| 4 | Landing gear secure and undamaged | ☐ |

---

### 2.2 Motors

| # | Check | Done |
|---|---|---|
| 1 | Motor 1 (Front-Right) — mounting screws tight | ☐ |
| 2 | Motor 2 (Rear-Left) — mounting screws tight | ☐ |
| 3 | Motor 3 (Front-Left) — mounting screws tight | ☐ |
| 4 | Motor 4 (Rear-Right) — mounting screws tight | ☐ |
| 5 | All motors spin freely by hand — no grinding or stiffness | ☐ |
| 6 | Motor screws not protruding into motor windings | ☐ |
| 7 | Motor bell has no wobble when spun by hand | ☐ |

---

### 2.3 Propellers

| # | Check | Done |
|---|---|---|
| 1 | Props are correct type and size for your motors | ☐ |
| 2 | Props have no cracks, chips, or warping | ☐ |
| 3 | CW props on Motor 1 (FR) and Motor 2 (RL) | ☐ |
| 4 | CCW props on Motor 3 (FL) and Motor 4 (RR) | ☐ |
| 5 | Prop nuts/bolts tight — cannot be pulled off by hand | ☐ |
| 6 | Props balanced (no visible weight imbalance) | ☐ |

> **Propeller spin direction must match motor spin direction.**
> Incorrect props will cause crash on takeoff.

---

### 2.4 Electronics & Wiring

| # | Check | Done |
|---|---|---|
| 1 | Flight controller mounted firmly (vibration dampers intact) | ☐ |
| 2 | All ESC connectors fully seated | ☐ |
| 3 | GPS module mounted and cable connected | ☐ |
| 4 | RC receiver mounted and cable connected to UART4 | ☐ |
| 5 | No loose wires that could contact propellers | ☐ |
| 6 | Battery cable connector in good condition | ☐ |
| 7 | No exposed solder joints or bare wires | ☐ |
| 8 | IMU sensor (ICM-45686) SPI cable secure | ☐ |
| 9 | Barometer (BMP388) I2C cable secure | ☐ |
| 10 | Magnetometer (BMM150) I2C cable secure | ☐ |

---

## SECTION 3 — Power On Checks

### 3.1 Transmitter (Power on FIRST)

| # | Check | Done |
|---|---|---|
| 1 | Transmitter powered on | ☐ |
| 2 | All trims set to zero / center | ☐ |
| 3 | Throttle stick at minimum (full down) | ☐ |
| 4 | All switches in safe/default position | ☐ |
| 5 | Kill switch (CH11) in ACTIVE/KILL position | ☐ |
| 6 | Arm switch (CH10) in DISARMED position | ☐ |
| 7 | Flight mode switch (CH6) set to Stabilized | ☐ |
| 8 | TX battery level adequate | ☐ |

---

### 3.2 Flight Controller Boot (Connect Battery)

| # | Check | Done |
|---|---|---|
| 1 | QGroundControl open and ready on laptop/phone | ☐ |
| 2 | Connect USB or wait for telemetry link | ☐ |
| 3 | PX4 boot complete — no red error LEDs | ☐ |
| 4 | QGC connects and shows vehicle status | ☐ |
| 5 | No critical errors in QGC status bar | ☐ |
| 6 | Firmware version shows 1.17.0 alpha | ☐ |

---

## SECTION 4 — Sensor Checks

### 4.1 IMU (ICM-45686)

| # | Check | Done |
|---|---|---|
| 1 | QGC Attitude indicator is level on flat surface | ☐ |
| 2 | Tilt drone — attitude indicator responds correctly | ☐ |
| 3 | No "IMU inconsistency" error in QGC | ☐ |

**NSH Verification:**
```sh
listener sensor_combined
```
Expected: `accelerometer_m_s2[2]` ≈ **-9.8** (Z axis, gravity)

---

### 4.2 Barometer (BMP388)

| # | Check | Done |
|---|---|---|
| 1 | QGC shows altitude value (not 0 or NaN) | ☐ |
| 2 | No barometer error in QGC | ☐ |
| 3 | SYS_HAS_BARO = 1 confirmed in params | ☐ |

**NSH Verification:**
```sh
listener sensor_baro
```
Expected: `pressure` ≈ **1013 hPa** (adjust for your altitude)

---

### 4.3 Magnetometer (BMM150)

| # | Check | Done |
|---|---|---|
| 1 | QGC compass heading shows a direction | ☐ |
| 2 | Rotate drone — heading changes correctly | ☐ |
| 3 | No "compass inconsistency" error in QGC | ☐ |
| 4 | SYS_HAS_MAG = 1 confirmed in params | ☐ |

**NSH Verification:**
```sh
listener vehicle_magnetometer
```
Expected: Non-zero values on all three axes (X, Y, Z)

---

### 4.4 GPS (u-blox NEO-M8N)

| # | Check | Done |
|---|---|---|
| 1 | GPS module LED blinking (acquiring satellites) | ☐ |
| 2 | QGC shows GPS satellites count increasing | ☐ |
| 3 | Wait for 3D Fix — minimum 6 satellites | ☐ |
| 4 | HDOP < 2.0 in QGC GPS status | ☐ |
| 5 | Home position set (QGC shows home icon on map) | ☐ |

**NSH Verification:**
```sh
gps status
```
Expected:
```
fix_type:        3D Fix
satellites_used: ≥ 6
HDOP:            < 2.0
```

> **Do not take off until 3D Fix is achieved and home position is set.**

---

## SECTION 5 — RC Verification

### 5.1 RC Signal Check

| # | Check | Done |
|---|---|---|
| 1 | QGC shows RC connected (green RC bars) | ☐ |
| 2 | No RC signal warning in QGC | ☐ |

**NSH Verification:**
```sh
listener input_rc
```

### 5.2 Stick Response Check

Move each stick and verify correct channel responds:

| Stick Movement | Channel | Expected Range | Done |
|---|---|---|---|
| Right stick — Left/Right (Roll) | CH1 | 1000 – 2000 µs | ☐ |
| Right stick — Up/Down (Pitch) | CH2 | 1000 – 2000 µs | ☐ |
| Left stick — Up/Down (Throttle) | CH3 | 1000 – 2000 µs | ☐ |
| Left stick — Left/Right (Yaw) | CH4 | 1000 – 2000 µs | ☐ |
| Flight mode switch (CH6) | CH6 | Changes mode | ☐ |
| Arm switch (CH10) | CH10 | 1000 / 2000 µs | ☐ |
| Kill switch (CH11) | CH11 | 1000 / 2000 µs | ☐ |

### 5.3 Stick Direction Verification

| Movement | Expected Drone Response |
|---|---|
| Roll right (right stick right) | Right side dips, left rises |
| Pitch forward (right stick up) | Nose dips forward |
| Yaw right (left stick right) | Nose rotates clockwise |
| Throttle up (left stick up) | All motors speed up equally |

> **Verify this in QGC attitude indicator before arming.**
> If any axis is reversed, correct `RC_X_REV` parameter before flying.

### 5.4 RC Failsafe Test

| # | Check | Done |
|---|---|---|
| 1 | Turn off transmitter while QGC is connected | ☐ |
| 2 | QGC shows "RC LOST" within 0.5 seconds | ☐ |
| 3 | Turn transmitter back on — RC restored | ☐ |

---

## SECTION 6 — Motor Check (NO PROPELLERS)

> **CRITICAL: Remove propellers before this section.**

### 6.1 Individual Motor Test

```sh
actuator_test set -m 1 -v 0.1 -t 2    # Motor 1 - Front Right
actuator_test set -m 2 -v 0.1 -t 2    # Motor 2 - Rear Left
actuator_test set -m 3 -v 0.1 -t 2    # Motor 3 - Front Left
actuator_test set -m 4 -v 0.1 -t 2    # Motor 4 - Rear Right
```

| # | Check | Done |
|---|---|---|
| 1 | Motor 1 (Front-Right) spins — correct location | ☐ |
| 2 | Motor 2 (Rear-Left) spins — correct location | ☐ |
| 3 | Motor 3 (Front-Left) spins — correct location | ☐ |
| 4 | Motor 4 (Rear-Right) spins — correct location | ☐ |
| 5 | All motors spin smoothly — no vibration/noise | ☐ |
| 6 | No excessive heat after test | ☐ |

### 6.2 Motor Spin Direction Check

Use a small piece of tape or marker on each motor shaft to verify direction.

| Motor | Location | Required Spin | Confirmed |
|---|---|---|---|
| Motor 1 | Front-Right | Clockwise (CW) | ☐ |
| Motor 2 | Rear-Left | Clockwise (CW) | ☐ |
| Motor 3 | Front-Left | Counter-Clockwise (CCW) | ☐ |
| Motor 4 | Rear-Right | Counter-Clockwise (CCW) | ☐ |

> If any motor spins the wrong direction, swap any two of its three phase
> wires to reverse it.

---

## SECTION 7 — Pre-Arm Checks

### 7.1 Commander Check

```sh
commander check
```

All items must pass. Common issues and fixes:

| Failure Message | Fix |
|---|---|
| `GPS fix too low` | Wait for better GPS fix outdoors |
| `Compass not calibrated` | Run `commander calibrate mag` |
| `Accel not calibrated` | Run `commander calibrate accel` |
| `RC signal lost` | Check receiver binding and SBUS wiring |
| `Baro not healthy` | Check BMP388 I2C connection |
| `EKF not initialized` | Wait 30 seconds after boot |

### 7.2 QGC Pre-Arm Status

| # | Check | Done |
|---|---|---|
| 1 | QGC status bar shows green — ready to fly | ☐ |
| 2 | No red warnings in QGC | ☐ |
| 3 | EKF2 initialized — `xy_valid = 1`, `z_valid = 1` | ☐ |
| 4 | Battery level acceptable (> 50% for first flight) | ☐ |
| 5 | Home position set on QGC map | ☐ |

**NSH EKF2 check:**
```sh
listener vehicle_local_position
```
Expected: `xy_valid: 1` and `z_valid: 1`

---

## SECTION 8 — Arming & First Hover

### 8.1 Final Safety Check Before Arming

| # | Check | Done |
|---|---|---|
| 1 | Propellers installed and tight | ☐ |
| 2 | All people are 10+ metres away | ☐ |
| 3 | Pilot is behind the drone | ☐ |
| 4 | Flight mode set to Stabilized | ☐ |
| 5 | Throttle at minimum | ☐ |
| 6 | Kill switch ready in hand | ☐ |

### 8.2 Arming Procedure

```
1. Set flight mode → Stabilized (CH6)
2. Flip Arm switch (CH10) → ARMED
3. QGC confirms ARMED status
4. Motors spin at idle speed
5. Do NOT increase throttle yet
6. Verify all 4 motors spinning at idle
7. Slowly increase throttle to hover point (~50%)
```

### 8.3 First Hover Checks (< 1 metre altitude)

| # | Check | Done |
|---|---|---|
| 1 | Drone lifts off without aggressive movement | ☐ |
| 2 | Hovers stable with minimal stick input | ☐ |
| 3 | Roll correction (stick right → drone moves right) | ☐ |
| 4 | Pitch correction (stick forward → drone moves forward) | ☐ |
| 5 | Yaw responds correctly (stick left → rotates CCW) | ☐ |
| 6 | No oscillations or toilet-bowling | ☐ |
| 7 | No unusual motor noise or vibration | ☐ |

> **Keep first hover under 1 metre altitude.**
> **Keep flight time under 2 minutes for first test.**

---

## SECTION 9 — Landing & Post-Flight

### 9.1 Landing Procedure

```
1. Reduce throttle gradually
2. Descend slowly to ground
3. Full throttle down on touchdown
4. Arm switch → DISARM (or auto-disarm after 10s)
5. Kill switch to ACTIVE before approaching drone
6. Wait for all motors to fully stop
7. Disconnect battery FIRST
8. Power off transmitter LAST
```

### 9.2 Post-Flight Inspection

| # | Check | Done |
|---|---|---|
| 1 | All motors — check for heat (hand test) | ☐ |
| 2 | ESCs — no burning smell | ☐ |
| 3 | Frame — check for new cracks or damage | ☐ |
| 4 | Props — check for chips or cracks | ☐ |
| 5 | Battery — check for puffing or heat | ☐ |
| 6 | Download flight log from SD card / QGC | ☐ |

### 9.3 Post-Flight Data Review

```sh
# Review flight log in QGC (Analyze → Log Download)
# Check for:
# - EKF2 health flags
# - Vibration levels (should be < 30 m/s² on all axes)
# - Battery sag during flight
# - Any error messages during flight
```

---

## QUICK REFERENCE — Emergency Actions

| Situation | Action |
|---|---|
| **Drone out of control** | Kill switch (CH11) → all motors stop |
| **Motor failure in air** | Kill switch immediately |
| **RC signal lost** | Drone auto-RTL (if COM_RCL_ACT=2) |
| **Low battery warning** | Land immediately — don't wait |
| **Fly-away** | Kill switch → let it crash (safer than fly-away) |
| **Fire on drone** | Kill switch → stand back → fire extinguisher |

---

## Flight Log

| Field | Entry |
|---|---|
| Date | |
| Location | |
| Battery voltage (start) | |
| Battery voltage (end) | |
| Flight duration | |
| Max altitude | |
| Issues observed | |
| Actions taken | |

---

*Checklist version 1.0 — SAMV71-XULT Quadrotor — PX4 v1.17.0 alpha*
*Board: microchip/samv71-xult-clickboards — Branch: samv7-custom*
