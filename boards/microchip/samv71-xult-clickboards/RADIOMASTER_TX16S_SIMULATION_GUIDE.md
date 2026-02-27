# RadioMaster TX16S — Gazebo & QGroundControl Simulation Guide

**Board:** SAMV71-XULT with PX4
**Radio:** RadioMaster TX16S (EdgeTX)
**Simulation:** PX4 SITL / HITL with Gazebo Classic

---

## Table of Contents

1. [Overview](#1-overview)
2. [Prerequisites](#2-prerequisites)
3. [System Architecture](#3-system-architecture)
4. [TX16S USB Joystick Setup](#4-tx16s-usb-joystick-setup)
5. [Linux USB Verification](#5-linux-usb-verification)
6. [QGroundControl Installation](#6-qgroundcontrol-installation)
7. [PX4 SITL + Gazebo Setup](#7-px4-sitl--gazebo-setup)
8. [QGroundControl Joystick Configuration](#8-qgroundcontrol-joystick-configuration)
9. [HITL Setup with SAMV71](#9-hitl-setup-with-samv71)
10. [Flight Testing in Simulation](#10-flight-testing-in-simulation)
11. [Troubleshooting](#11-troubleshooting)
12. [Quick Reference](#12-quick-reference)

---

## 1. Overview

This guide walks through two simulation modes using the RadioMaster TX16S radio controller:

| Mode | PX4 Runs On | Hardware Needed | Best For |
|------|-------------|-----------------|----------|
| **SITL** | PC (Linux process) | TX16S + USB cable | Algorithm development, learning |
| **HITL** | SAMV71 (real FC) | TX16S + ELRS receiver + SAMV71 | Pre-flight firmware validation |

In both modes:
- **Gazebo** simulates the drone physics, sensors, and 3D world
- **TX16S** provides real stick inputs exactly as in actual flight
- **QGroundControl** connects everything and provides GCS telemetry view

---

## 2. Prerequisites

### Hardware
- RadioMaster TX16S (running EdgeTX firmware)
- USB-C cable (TX16S to PC)
- PC running Ubuntu 20.04 / 22.04
- *(HITL only)* ELRS receiver + SAMV71 board + USB cable for FC

### Software
```bash
# Check Ubuntu version
lsb_release -a

# Install build dependencies (if not already done)
sudo apt update
sudo apt install -y git cmake ninja-build python3-pip python3-dev \
                   joystick jstest-gtk

# Install Gazebo Classic
sudo apt install -y gazebo libgazebo-dev

# Python dependencies for PX4
pip3 install kconfiglib jinja2 jsonschema pyserial
```

### PX4 Repository
```bash
# Ensure you are on the correct branch
cd ~/linux_day2/PX4-Autopilot-Private
git status
# Should show: On branch samv7-custom
```

---

## 3. System Architecture

### SITL Mode (No Real FC)

```
┌─────────────────────────────────────────────────────────────┐
│                        Your PC                              │
│                                                             │
│  ┌───────────────┐    MAVLink UDP    ┌──────────────────┐  │
│  │   Gazebo       │◄────────────────►│  PX4 SITL        │  │
│  │   Physics +    │                  │  Process         │  │
│  │   3D World     │                  │  EKF2            │  │
│  └───────────────┘                   │  Commander       │  │
│          ▲                           │  Controllers     │  │
│          │                           └──────────────────┘  │
│  ┌───────────────┐    MAVLink UDP             ▲            │
│  │ QGroundControl │◄──────────────────────────┘            │
│  │                │                                        │
│  │  Joystick API  │                                        │
│  └───────┬───────┘                                         │
│          │ USB HID                                         │
└──────────┼─────────────────────────────────────────────────┘
           │
     ┌─────┴──────┐
     │ TX16S      │
     │ (Joystick  │
     │  mode)     │
     └────────────┘
```

### HITL Mode (With SAMV71 Real FC)

```
┌──────────── PC ─────────────────┐      ┌───── SAMV71 (Real FC) ──────┐
│                                 │      │                             │
│  Gazebo ◄──── MAVLink USB ─────────────►  PX4 Flight Stack          │
│  Physics                        │      │  EKF2, Commander,           │
│  3D World                       │      │  mc_att_control             │
│                                 │      │           ▲                 │
│  QGroundControl                 │      │           │ CRSF/SBUS       │
│                                 │      │      UART4 (PC29)           │
└─────────────────────────────────┘      └───────────┼─────────────────┘
                                                      │ RF 2.4GHz
                                              ┌───────┴──────┐
                                              │ ELRS Receiver │
                                              └───────────────┘
                                                      ▲ RF
                                              ┌───────┴──────┐
                                              │  TX16S        │
                                              │  (RF mode)    │
                                              └──────────────┘
```

---

## 4. TX16S USB Joystick Setup

### 4.1 Enable HID Joystick Mode in EdgeTX

1. Power on the TX16S
2. **Long press** the `SYS` button (top-left)
3. Navigate to the **Hardware** tab (swipe or scroll right)
4. Find **USB Mode** setting
5. Change it to **HID Joystick**
6. Press `EXIT` or `RTN` to save

```
TX16S Screen:
┌─────────────────────────┐
│ HARDWARE                │
│                         │
│ Internal RF:  ELRS      │
│ External RF:  OFF       │
│ USB Mode:     HID Joystick  ◄── Set this
│                         │
└─────────────────────────┘
```

> **Note:** If you see only "Serial" and "MIDI" options, update your EdgeTX firmware.
> EdgeTX 2.8+ has HID Joystick mode.

### 4.2 Connect USB Cable

- Connect TX16S USB-C port (bottom of radio) to your PC
- TX16S screen shows: **"USB Connected"**
- Confirm **"HID Joystick"** on the USB mode dialog that appears

### 4.3 Verify Stick Mode

Ensure your TX16S is in **Mode 2** (most common):
```
Left Stick:   Throttle (up/down) + Yaw (left/right)
Right Stick:  Pitch (up/down)    + Roll (left/right)
```

To check/change stick mode:
```
SYS → Hardware → Stick Mode → Mode 2
```

---

## 5. Linux USB Verification

### 5.1 Check Device Detected

```bash
# List joystick devices
ls -la /dev/input/js*
# Expected: /dev/input/js0

# Check USB device
lsusb | grep -i radiomaster
# Expected: Bus 00X Device 00X: ID xxxx:xxxx RadioMaster TX16S
```

### 5.2 Add User to Input Group

```bash
# Add yourself to input group (do once, then re-login)
sudo usermod -aG input $USER
sudo usermod -aG dialout $USER

# Re-login or use newgrp
newgrp input
```

### 5.3 Test Stick Inputs

```bash
# Install joystick test tool
sudo apt install -y joystick

# Test TX16S axes and buttons
jstest /dev/input/js0
```

Expected output when moving sticks:
```
Axes:  0:     0  1:     0  2:     0  3: -32767  ...
         Roll    Pitch    Yaw      Throttle
```

Move each stick — values should change between **-32767** and **+32767**.

### 5.4 Graphical Test (Optional)

```bash
sudo apt install jstest-gtk
jstest-gtk
# Select js0 from the list
# Visual bars show stick positions
```

---

## 6. QGroundControl Installation

### 6.1 Download QGC

```bash
# Create a tools directory
mkdir -p ~/tools && cd ~/tools

# Download latest QGroundControl AppImage
# Go to: https://docs.qgroundcontrol.com/master/en/getting_started/download_and_install.html
# Download the Linux AppImage

# Make executable
chmod +x QGroundControl.AppImage
```

### 6.2 Install Dependencies

```bash
sudo apt install -y libsdl2-dev libgstreamer1.0-dev \
                   gstreamer1.0-plugins-bad \
                   gstreamer1.0-libav \
                   gstreamer1.0-gl \
                   libqt5gui5
```

### 6.3 Launch QGC

```bash
./QGroundControl.AppImage
```

---

## 7. PX4 SITL + Gazebo Setup

### 7.1 Build PX4 SITL

```bash
cd ~/linux_day2/PX4-Autopilot-Private

# Build SITL with Gazebo Classic (quadcopter)
make px4_sitl_default gazebo-classic

# Alternative: specific airframe
make px4_sitl_default gazebo-classic_iris
```

This command:
- Compiles PX4 as a native Linux binary
- Launches Gazebo with the iris quadcopter model
- Starts the PX4 SITL process
- Opens MAVLink on UDP port **14550** (for QGC) and **14560** (for Gazebo)

### 7.2 Expected Output

```
[gazebo] Waiting for simulation to start
INFO  [simulator_mavlink] Simulator connected on UDP port 14560
INFO  [commander] LED: open /dev/led0 failed (22)
INFO  [mavlink] mode: Normal, data rate: 4000000 B/s ...
INFO  [logger] logger started (mode=all)
pxh>
```

The `pxh>` prompt means PX4 SITL is running and accepting commands.

### 7.3 Verify Gazebo Window

Gazebo should open showing a quadcopter on a flat plane:

```
┌──────────────────────────────┐
│   Gazebo Classic             │
│                              │
│      [Drone model]           │
│         /  \                 │
│        /    \                │
│       ●      ●               │
│                              │
│   World  |  Models  |  ...   │
└──────────────────────────────┘
```

---

## 8. QGroundControl Joystick Configuration

### 8.1 Connect QGC to SITL

QGC auto-connects to PX4 SITL via UDP port 14550.

Status bar should show:
```
● Connected  |  SITL  |  Disarmed
```

### 8.2 Open Joystick Settings

```
Click Q (top-left icon)
  → Application Settings
  → Joystick (left sidebar)
```

### 8.3 Enable Joystick

```
┌─────────────────────────────────────────────────────────┐
│ JOYSTICK                                                 │
│                                                          │
│  Enabled           [  ✓  ]                              │
│  Device:           [ RadioMaster TX16S  ▼ ]             │
│                                                          │
│  Active joystick   ●  RadioMaster TX16S                 │
└─────────────────────────────────────────────────────────┘
```

Toggle **Enabled** to ON and select your TX16S from the Device dropdown.

### 8.4 Calibrate Axes

1. Click **Calibrate** button
2. Follow on-screen instructions:

```
Step 1: Move ALL sticks to their MAXIMUM extents
        (push to all four corners, full throttle up/down)
        Click Next

Step 2: Center all sticks
        (let go of sticks, throttle to center or bottom)
        Click Next

Step 3: Move throttle FULL UP then FULL DOWN
        Click Next

Step 4: Click Finish
```

### 8.5 Verify Axis Mapping

After calibration, move each stick and confirm the green bars respond correctly:

```
┌───────────────────────────────────────────────────────────┐
│ AXIS MAPPING                                               │
│                                                            │
│ Roll      Axis [0 ▼]  [  ] Reversed    ████░░░░  center  │
│ Pitch     Axis [1 ▼]  [✓ ] Reversed    ████░░░░  center  │
│ Yaw       Axis [2 ▼]  [  ] Reversed    ████░░░░  center  │
│ Throttle  Axis [3 ▼]  [✓ ] Reversed    ░░░░░░░░  bottom  │
└───────────────────────────────────────────────────────────┘
```

**Common axis reversals needed:**
- Pitch: usually needs **Reversed** checked
- Throttle: usually needs **Reversed** checked

### 8.6 Set Deadband

Add a small deadband to prevent stick drift:
```
Deadband: 0.05  (5%)
```

### 8.7 Assign Buttons (Recommended)

In the **Button Assignment** section:

| Button | TX16S Location | Assign To |
|--------|---------------|-----------|
| Button 0 | SF (top-left) | Arm / Disarm |
| Button 1 | SA (left shoulder) | Stabilized mode |
| Button 2 | SB (right shoulder) | Position Hold |
| Button 3 | SC | Return to Launch |
| Button 4 | SD | Mission mode |

To find which button number maps to which switch:
```bash
jstest /dev/input/js0
# Flip each switch and note which button number changes
```

### 8.8 Enable Joystick for RC Override

In QGC Joystick settings, ensure:
```
✓ Use joystick information to control vehicle
  (sends RC_CHANNELS_OVERRIDE MAVLink messages)
```

---

## 9. HITL Setup with SAMV71

### 9.1 Hardware Connections

```
TX16S internal ELRS module
      │  2.4GHz RF
      ▼
ELRS Receiver (e.g. BetaFPV SuperD, ELRS EP1/EP2)
      │  CRSF serial output (3.3V logic)
      ▼
SAMV71 UART4 pin PC29 (RC Input)
      │  (also connect GND and 3.3V to receiver)

SAMV71 USB-C
      │  USB cable
      ▼
PC (running Gazebo + QGC)
```

### 9.2 ELRS Receiver Wiring

| ELRS Receiver Pin | SAMV71 Pin | Notes |
|-------------------|------------|-------|
| TX (CRSF out) | PC29 (UART4 RX) | Signal wire |
| GND | GND | Common ground |
| 5V or 3.3V | 3.3V | Check receiver voltage requirement |

### 9.3 Bind TX16S to ELRS Receiver

1. Power receiver in bind mode (hold bind button while powering)
2. On TX16S: `SYS → Internal RF → ELRS → Bind`
3. Wait for solid LED on receiver (bound)
4. Power cycle both — receiver LED should be solid

### 9.4 Enable HITL Mode on SAMV71

Flash SAMV71 with your firmware, then in QGC:
```
Vehicle Setup → Parameters → Search: SYS_HITL
SYS_HITL = 1
Reboot FC
```

> **Important:** With `SYS_HITL=1`, real onboard sensors (ICM45686, BMP388) are disabled.
> Sensor data comes from Gazebo via MAVLink HIL_SENSOR messages.

### 9.5 Launch HITL Gazebo

```bash
# Launch Gazebo in HITL mode (connects to real FC via serial)
cd ~/linux_day2/PX4-Autopilot-Private

# Find your SAMV71 serial port
ls /dev/ttyACM*
# Usually: /dev/ttyACM0

# Launch HITL
make px4_sitl_default gazebo-classic
# Then in PX4 NSH shell:
# pxh> mavlink start -d /dev/ttyACM0 -b 921600 -m onboard
```

### 9.6 HITL Data Flow

```
TX16S sticks
    │ RF
    ▼
ELRS Receiver → SAMV71 UART4 (CRSF)
    │
    ▼
PX4 rc_input driver (real hardware)
    │ uORB: input_rc
    ▼
Commander → Flight mode selection
    │
mc_att_control → attitude commands
    │
HIL_ACTUATOR_CONTROLS (MAVLink → USB)
    │
Gazebo physics → drone moves
    │
HIL_SENSOR (MAVLink → USB back to SAMV71)
    │
EKF2 on SAMV71 (real code, fake sensor data)
    │ (loop continues)
```

---

## 10. Flight Testing in Simulation

### 10.1 Pre-Flight Checks

```bash
# In PX4 SITL terminal (pxh> prompt):
commander check         # run preflight checks
param show RC_MAP_THROTTLE   # verify RC mapping
param show COM_RC_IN_MODE    # check RC input mode
```

For SITL with joystick:
```
COM_RC_IN_MODE = 1   (Joystick/No RC Checks)
```

### 10.2 Arming and Takeoff

**Via TX16S sticks (Mode 2):**
```
To ARM:
  Left stick → bottom-right corner (hold 2 seconds)
  OR press assigned ARM button

To TAKEOFF:
  Slowly raise left stick (throttle) past 50%
  Drone lifts off in Gazebo

To LAND:
  Lower throttle slowly until drone descends
  OR: Switch to Land mode via button
```

**Via QGC:**
```
Click ARM button in QGC toolbar
Then raise throttle on TX16S
```

### 10.3 Flight Modes to Test

| Mode | How to Set | What to Test |
|------|-----------|--------------|
| Stabilized | Switch SA down | Manual attitude control |
| Altitude Hold | Switch SA middle | Throttle = altitude, sticks = attitude |
| Position Hold | Switch SA up | Sticks = velocity, auto hover when centered |
| Return to Launch | Switch SC up | Autonomous return to home |
| Mission | Switch SD up | Autonomous waypoint mission |

### 10.4 Monitoring in QGC

```
QGC HUD shows:
├── Attitude (roll, pitch, yaw)
├── Altitude (from simulated baro/GPS)
├── Airspeed / groundspeed
├── Battery (simulated)
├── RC signal strength
├── Flight mode
└── Armed / Disarmed state
```

---

## 11. Troubleshooting

### TX16S Not Detected by Linux

```bash
# Check USB mode on TX16S
# Must be: SYS → Hardware → USB Mode → HID Joystick

# Check dmesg for USB events
dmesg | tail -20

# Check if device exists
ls /dev/input/js*

# Fix permissions
sudo chmod 666 /dev/input/js0
# Or permanently:
sudo usermod -aG input $USER
```

### QGC Does Not Show TX16S in Joystick List

- QGC must be connected to a vehicle (SITL or real FC) first
- Try: close QGC → launch SITL first → then open QGC
- Unplug and re-plug TX16S USB cable
- Restart QGC

### Axes Not Responding in QGC

```bash
# Verify jstest shows axis movement
jstest /dev/input/js0
# Move sticks — values must change

# If axes show 0 always → TX16S not in Joystick mode
# Re-check EdgeTX: SYS → Hardware → USB Mode
```

### Drone Not Responding to Stick Inputs

```
Check in QGC → Vehicle Setup → Parameters:
  COM_RC_IN_MODE = 1    (Joystick mode)

Check in QGC Joystick page:
  ✓ Enabled is ON
  ✓ Green bars move when sticks move

Check in PX4 NSH shell:
  pxh> listener input_rc
  (should show RC channel values changing)
```

### Throttle / Pitch Reversed

```
QGC → Application Settings → Joystick
  → Check "Reversed" for the affected axis
  → Throttle almost always needs Reversed
  → Pitch almost always needs Reversed
```

### Gazebo Not Launching

```bash
# Kill any stuck Gazebo instances
pkill -9 gzserver
pkill -9 gzclient

# Clean build
cd ~/linux_day2/PX4-Autopilot-Private
make clean
make px4_sitl_default gazebo-classic
```

### HITL — Accel TIMEOUT Error

```
Cause: SYS_HITL=1 set but Gazebo/HIL_SENSOR not connected

Fix:
  1. Ensure Gazebo is running and connected to SAMV71 via USB
  2. Check MAVLink link is active:
     pxh> mavlink status
  3. Verify HIL messages flowing:
     pxh> listener sensor_accel  (should show data)
```

### HITL — RC Not Working

```
Cause: ELRS receiver not bound or wrong UART

Fix:
  1. Re-bind ELRS receiver to TX16S
  2. Check UART4 is enabled in defconfig:
     grep UART4 boards/microchip/samv71-xult-clickboards/nuttx-config/nsh/defconfig
  3. Check RC input in PX4:
     pxh> rc_input start
     pxh> listener input_rc
```

---

## 12. Quick Reference

### Launch Commands

```bash
# SITL + Gazebo (quadcopter)
make px4_sitl_default gazebo-classic

# SITL + Gazebo (specific model)
make px4_sitl_default gazebo-classic_iris

# Clean and rebuild SITL
make clean && make px4_sitl_default gazebo-classic
```

### Useful PX4 NSH Commands

```bash
# Check RC input
listener input_rc

# Check sensor data (SITL)
listener sensor_accel
listener sensor_gyro
listener vehicle_gps_position

# Check flight mode
listener vehicle_status

# Check actuator outputs
listener actuator_outputs

# Manual arm (SITL only)
commander arm

# Manual takeoff (SITL only)
commander takeoff
```

### Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `SYS_HITL` | 0=SITL, 1=HITL | Simulation mode |
| `COM_RC_IN_MODE` | 1 | Joystick input mode |
| `RC_MAP_THROTTLE` | 3 | Throttle on channel 3 |
| `RC_MAP_ROLL` | 1 | Roll on channel 1 |
| `RC_MAP_PITCH` | 2 | Pitch on channel 2 |
| `RC_MAP_YAW` | 4 | Yaw on channel 4 |
| `COM_ARM_AUTH_REQ` | 0 | Disable arm auth for sim |

### TX16S EdgeTX Quick Settings

```
USB Joystick Mode:  SYS → Hardware → USB Mode → HID Joystick
Stick Mode:         SYS → Hardware → Stick Mode → Mode 2
ELRS Bind:          SYS → Internal RF → ELRS → Bind
RF Power:           SYS → Internal RF → Power → 10mW (for indoor)
```

### Port Reference

| Service | Protocol | Port |
|---------|----------|------|
| QGC ↔ PX4 SITL | MAVLink UDP | 14550 |
| Gazebo ↔ PX4 SITL | MAVLink UDP | 14560 |
| HITL FC serial | MAVLink UART | /dev/ttyACM0 |
| TX16S joystick | USB HID | /dev/input/js0 |

---

*Generated for SAMV71-XULT PX4 Custom Board — samv7-custom branch*
