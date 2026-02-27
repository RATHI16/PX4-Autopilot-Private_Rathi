# RC Calibration Guide — SAMV71 + TX16S (SBUS)

## Hardware Setup

- **Transmitter:** RadioMaster TX16S (Mode 2)
- **Receiver:** RadioMaster R81 V2 (or compatible)
- **Protocol:** SBUS (inverted serial, requires inverter circuit)
- **UART:** UART4 on SAMV71 — /dev/ttyS3 — Pin PD18 (J505 Pin 3)
- **Baud:** 100000, 8-bit, even parity, 2 stop bits

### SBUS Inverter Circuit (Required)

SBUS is inverted UART. An external inverter is needed between receiver and PD18.

```
                  3.3V
                   |
                 [10k]
                   |
SBUS Out --[1k]--B |C----------> PD18 (J505 Pin 3)
                   |
                  E |
                   |
                  GND

        (NPN transistor: BC547, 2N2222, or similar)
```

### Wiring

| From Receiver | To SAMV71-XULT          |
|---------------|-------------------------|
| SBUS signal   | Inverter -> J505 Pin 3  |
| VCC           | 5V rail                 |
| GND           | GND rail                |

---

## TX16S Channel Mapping

Set these in TX16S under Model Setup > Mixes:

| Channel | Source        | Function           |
|---------|--------------|--------------------|
| CH1     | Ail          | Roll               |
| CH2     | Ele          | Pitch              |
| CH3     | Thr          | Throttle           |
| CH4     | Rud          | Yaw                |
| CH5     | SF (2-pos)   | Aux 2              |
| CH6     | SB (3-pos)   | Flight Mode        |
| CH7     | SC (3-pos)   | Return-to-Home     |
| CH8     | SG (3-pos)   | Aux 3              |
| CH9     | SE (3-pos)   | Aux 1              |
| CH10    | SA (2-pos)   | Arm/Disarm         |
| CH11    | SD (2-pos)   | Kill Switch        |
| CH12    | SH (momentary)| Aux 4             |

TX16S Output: Model Setup > Internal RF > Output > SBUS

---

## PX4 Parameters — Complete List

Run all commands below on the NSH console (USB terminal).

### 1. Main Flight Controls (DO NOT CHANGE)

```
param set RC_MAP_ROLL      1
param set RC_MAP_PITCH     2
param set RC_MAP_THROTTLE  3
param set RC_MAP_YAW       4
```

| Param            | Channel | TX16S Stick              | Function |
|------------------|---------|--------------------------|----------|
| RC_MAP_ROLL      | 1       | Right stick left/right   | Aileron  |
| RC_MAP_PITCH     | 2       | Right stick up/down      | Elevator |
| RC_MAP_THROTTLE  | 3       | Left stick up/down       | Throttle |
| RC_MAP_YAW       | 4       | Left stick left/right    | Rudder   |

### 2. Switch Mapping

```
param set RC_MAP_ARM_SW     10
param set RC_MAP_FLTMODE    6
param set RC_MAP_RETURN_SW  7
param set RC_MAP_KILL_SW    11
param set RC_MAP_AUX1       9
param set RC_MAP_AUX2       5
param set RC_MAP_AUX3       8
param set RC_MAP_AUX4       12
```

| Param              | Channel | TX16S Switch | Function                        |
|--------------------|---------|--------------|---------------------------------|
| RC_MAP_ARM_SW      | 10      | SA (2-pos)   | Arm/Disarm                      |
| RC_MAP_FLTMODE     | 6       | SB (3-pos)   | Flight Mode Selection           |
| RC_MAP_RETURN_SW   | 7       | SC (3-pos)   | Return-to-Home                  |
| RC_MAP_KILL_SW     | 11      | SD (2-pos)   | Emergency Kill                  |
| RC_MAP_AUX1        | 9       | SE (3-pos)   | Auxiliary 1                     |
| RC_MAP_AUX2        | 5       | SF (2-pos)   | Auxiliary 2                     |
| RC_MAP_AUX3        | 8       | SG (3-pos)   | Auxiliary 3                     |
| RC_MAP_AUX4        | 12      | SH (momentary)| Auxiliary 4                    |

### 3. Flight Mode Slots

```
param set COM_FLTMODE1  8
param set COM_FLTMODE4  1
param set COM_FLTMODE6  2
```

| SB Position | Slot | Mode              | Value |
|-------------|------|-------------------|-------|
| Down        | 1    | Stabilized        | 8     |
| Middle      | 4    | Altitude Hold     | 1     |
| Up          | 6    | Position Hold     | 2     |

Available mode values: 0=Manual, 1=Altitude, 2=Position, 3=Mission,
4=Hold, 5=Return, 6=Acro, 7=Offboard, 8=Stabilized

### 4. Arming Behavior

```
param set COM_ARM_SWISBTN  0
```

0 = Toggle switch (flip to arm, flip back to disarm)
1 = Momentary button (hold to arm)

### 5. Channel Count

```
param set RC_CHAN_CNT  12
```

---

## RC Channel Calibration

### How to Get Your Values

Run `listener input_rc` on the NSH console. Move each stick/switch to its
extremes and write down the values. Typical SBUS range: 172 to 1811, center 992.

### Stick Channels

```
param set RC1_MIN   172
param set RC1_TRIM  992
param set RC1_MAX   1811
param set RC1_REV   1.0

param set RC2_MIN   172
param set RC2_TRIM  992
param set RC2_MAX   1811
param set RC2_REV   1.0

param set RC3_MIN   172
param set RC3_TRIM  172
param set RC3_MAX   1811
param set RC3_REV   1.0

param set RC4_MIN   172
param set RC4_TRIM  992
param set RC4_MAX   1811
param set RC4_REV   1.0
```

IMPORTANT: RC3_TRIM (Throttle) must equal RC3_MIN. Throttle has no center.

Replace the example values (172, 992, 1811) with your actual values from
`listener input_rc`.

### Switch Channels

```
param set RC5_MIN   172
param set RC5_TRIM  172
param set RC5_MAX   1811
param set RC5_REV   1.0

param set RC6_MIN   172
param set RC6_TRIM  992
param set RC6_MAX   1811
param set RC6_REV   1.0

param set RC7_MIN   172
param set RC7_TRIM  992
param set RC7_MAX   1811
param set RC7_REV   1.0

param set RC8_MIN   172
param set RC8_TRIM  992
param set RC8_MAX   1811
param set RC8_REV   1.0

param set RC9_MIN   172
param set RC9_TRIM  992
param set RC9_MAX   1811
param set RC9_REV   1.0

param set RC10_MIN  172
param set RC10_TRIM 172
param set RC10_MAX  1811
param set RC10_REV  1.0

param set RC11_MIN  172
param set RC11_TRIM 172
param set RC11_MAX  1811
param set RC11_REV  1.0

param set RC12_MIN  172
param set RC12_TRIM 172
param set RC12_MAX  1811
param set RC12_REV  1.0
```

Note: 2-position switches (SA, SD, SF, SH) use TRIM = MIN.
3-position switches (SB, SC, SE, SG) use TRIM = center value.

---

## Save and Reboot

```
param save
reboot
```

---

## Verification Checklist

Run these after reboot to confirm everything works:

### Check 1: Raw RC data arriving

```
listener input_rc




```

Expected: 12 channels with values changing as you move sticks.
If all zeroes: fix PD18 pin conflict in init.c and SBUS inverter.

### Check 2: Sticks mapped correctly

```
listener manual_control_input
```

| Action              | Expected                  |
|---------------------|---------------------------|
| Throttle up         | z goes from 0.0 to 1.0   |
| Roll right          | y goes positive           |
| Pitch forward       | x goes positive           |
| Yaw right           | r goes positive           |

If a direction is backwards: change RCx_REV from 1.0 to -1.0.

### Check 3: Arm switch

```
listener vehicle_status
```

Flip SA up: arming_state should change.

### Check 4: Flight mode switch

```
listener vehicle_status
```

Flip SB to each position: nav_state should change between
8 (Stabilized), 1 (Altitude), 2 (Position).

### Check 5: Kill switch

```
listener actuator_armed
```

Flip SD up: force_failsafe should become true.

### Check 6: Return-to-Home switch

```
listener vehicle_status
```

Flip SC up: nav_state should change to 5 (Return).

---

## Troubleshooting

| Problem                          | Cause                  | Fix                              |
|----------------------------------|------------------------|----------------------------------|
| input_rc all zeroes              | PD18 pin conflict      | Fix init.c line 132: pass 0, 0  |
| input_rc all zeroes              | Missing SBUS inverter  | Build transistor inverter        |
| input_rc has data, sticks zero   | RC_MAP_* not set       | Set RC_MAP_ROLL/PITCH/THR/YAW   |
| Sticks mapped but manual_control zero | Calibration missing | Set RCx_MIN/TRIM/MAX values     |
| Stick moves wrong direction      | Channel reversed       | param set RCx_REV -1.0          |
| Throttle doesn't reach zero      | RC3_TRIM not at MIN    | param set RC3_TRIM = RC3_MIN    |
| Switch doesn't trigger           | Range too narrow       | Adjust RCx_MIN/RCx_MAX          |
| Flight mode stuck                | COM_FLTMODE not set    | Set COM_FLTMODE1, 4, 6          |
| Won't arm                        | Safety button not pressed | Press safety button (PA9) first |

---

## Quick Reference — All Parameters at a Glance

```
RC_MAP_ROLL=1  RC_MAP_PITCH=2  RC_MAP_THROTTLE=3  RC_MAP_YAW=4
RC_MAP_ARM_SW=10  RC_MAP_FLTMODE=6  RC_MAP_RETURN_SW=7  RC_MAP_KILL_SW=11
RC_MAP_AUX1=9  RC_MAP_AUX2=5  RC_MAP_AUX3=8  RC_MAP_AUX4=12
COM_FLTMODE1=8  COM_FLTMODE4=1  COM_FLTMODE6=2
COM_ARM_SWISBTN=0  RC_CHAN_CNT=12
```

